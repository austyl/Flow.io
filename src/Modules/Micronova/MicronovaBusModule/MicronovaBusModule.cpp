#include "Modules/Micronova/MicronovaBusModule/MicronovaBusModule.h"

#include <Arduino.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaBusModule)
#include "Core/ModuleLog.h"

namespace {

uint8_t micronovaUartIndex_(const BoardSpec& board)
{
    const UartSpec* spec = boardFindUart(board, "micronova");
    return spec ? spec->uartIndex : 1U;
}

}  // namespace

MicronovaBusModule::MicronovaBusModule(const BoardSpec& board)
    : serial_(micronovaUartIndex_(board)),
      rxPinVar_{NVS_KEY("mn_rx"), "rx_pin", "micronova/uart", ConfigType::Int32, &rxPin_, ConfigPersistence::Persistent, 0},
      txPinVar_{NVS_KEY("mn_tx"), "tx_pin", "micronova/uart", ConfigType::Int32, &txPin_, ConfigPersistence::Persistent, 0},
      enableRxPinVar_{NVS_KEY("mn_enrx"), "enable_rx_pin", "micronova/uart", ConfigType::Int32, &enableRxPin_, ConfigPersistence::Persistent, 0},
      baudrateVar_{NVS_KEY("mn_baud"), "baudrate", "micronova/serial", ConfigType::Int32, &baudrate_, ConfigPersistence::Persistent, 0},
      replyTimeoutVar_{NVS_KEY("mn_rpto"), "reply_timeout_ms", "micronova/serial", ConfigType::Int32, &replyTimeoutMsCfg_, ConfigPersistence::Persistent, 0},
      turnaroundDelayVar_{NVS_KEY("mn_turn"), "turnaround_delay_ms", "micronova/serial", ConfigType::Int32, &turnaroundDelayMsCfg_, ConfigPersistence::Persistent, 0},
      repeatDelayVar_{NVS_KEY("mn_rep"), "repeat_delay_ms", "micronova/serial", ConfigType::Int32, &repeatDelayMsCfg_, ConfigPersistence::Persistent, 0},
      offlineTimeoutVar_{NVS_KEY("mn_offto"), "offline_timeout_ms", "micronova/serial", ConfigType::Int32, &offlineTimeoutMsCfg_, ConfigPersistence::Persistent, 0}
{
    applyBoardDefaults_(board);
}

void MicronovaBusModule::applyBoardDefaults_(const BoardSpec& board)
{
    const UartSpec* spec = boardFindUart(board, "micronova");
    if (!spec) return;
    rxPin_ = spec->rxPin;
    txPin_ = spec->txPin;
    enableRxPin_ = spec->enableRxPin;
    baudrate_ = (int32_t)spec->baud;
}

void MicronovaBusModule::init(ConfigStore& cfg, ServiceRegistry&)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Micronova;
    cfg.registerVar(rxPinVar_, kCfgModuleId, 1);
    cfg.registerVar(txPinVar_, kCfgModuleId, 1);
    cfg.registerVar(enableRxPinVar_, kCfgModuleId, 1);
    cfg.registerVar(baudrateVar_, kCfgModuleId, 2);
    cfg.registerVar(replyTimeoutVar_, kCfgModuleId, 2);
    cfg.registerVar(turnaroundDelayVar_, kCfgModuleId, 2);
    cfg.registerVar(repeatDelayVar_, kCfgModuleId, 2);
    cfg.registerVar(offlineTimeoutVar_, kCfgModuleId, 2);
}

void MicronovaBusModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    LOGI("Micronova UART begin deferred");
}

void MicronovaBusModule::onStart(ConfigStore&, ServiceRegistry&)
{
    (void)begin();
}

bool MicronovaBusModule::begin()
{
    if (begun_) {
        serial_.end();
        begun_ = false;
    }

    if (enableRxPin_ >= 0) {
        pinMode((uint8_t)enableRxPin_, OUTPUT);
        setEnableRx_(false);
    }

    const uint32_t baud = (baudrate_ > 0) ? (uint32_t)baudrate_ : MicronovaProtocol::DefaultBaudrate;
    serial_.begin(baud, SERIAL_8N2, rxPin_, txPin_);
    serial_.setTimeout(0);
    while (serial_.available() > 0) {
        (void)serial_.read();
    }

    offlineTimeoutMs_ = (offlineTimeoutMsCfg_ > 0) ? (uint32_t)offlineTimeoutMsCfg_ : 600000UL;
    state_ = MicronovaBusState::Idle;
    replyLen_ = 0;
    begun_ = true;
    LOGI("Micronova UART begin baud=%lu rx=%ld tx=%ld en_rx=%ld",
         (unsigned long)baud,
         (long)rxPin_,
         (long)txPin_,
         (long)enableRxPin_);
    return true;
}

void MicronovaBusModule::loop()
{
    tick(millis());
}

bool MicronovaBusModule::pushCommand_(Ring<MicronovaCommand, ReadQueueCapacity>& q, const MicronovaCommand& cmd)
{
    if (q.count >= ReadQueueCapacity) return false;
    q.items[q.tail] = cmd;
    q.tail = (uint8_t)((q.tail + 1U) % ReadQueueCapacity);
    ++q.count;
    return true;
}

bool MicronovaBusModule::popRead_(MicronovaCommand& out)
{
    if (readQ_.count == 0U) return false;
    out = readQ_.items[readQ_.head];
    readQ_.head = (uint8_t)((readQ_.head + 1U) % ReadQueueCapacity);
    --readQ_.count;
    return true;
}

bool MicronovaBusModule::pushWrite_(const MicronovaCommand& cmd)
{
    if (writeQ_.count >= WriteQueueCapacity) return false;
    writeQ_.items[writeQ_.tail] = cmd;
    writeQ_.tail = (uint8_t)((writeQ_.tail + 1U) % WriteQueueCapacity);
    ++writeQ_.count;
    return true;
}

bool MicronovaBusModule::popWrite_(MicronovaCommand& out)
{
    if (writeQ_.count == 0U) return false;
    out = writeQ_.items[writeQ_.head];
    writeQ_.head = (uint8_t)((writeQ_.head + 1U) % WriteQueueCapacity);
    --writeQ_.count;
    return true;
}

bool MicronovaBusModule::pushValue_(const MicronovaRawValue& value)
{
    if (valueQ_.count >= ValueQueueCapacity) {
        valueQ_.head = (uint8_t)((valueQ_.head + 1U) % ValueQueueCapacity);
        --valueQ_.count;
    }
    valueQ_.items[valueQ_.tail] = value;
    valueQ_.tail = (uint8_t)((valueQ_.tail + 1U) % ValueQueueCapacity);
    ++valueQ_.count;
    return true;
}

bool MicronovaBusModule::queueRead(uint8_t readCode, uint8_t address)
{
    MicronovaCommand cmd{};
    cmd.code = readCode;
    cmd.address = address;
    cmd.write = false;
    cmd.repeatCount = 1;
    cmd.repeatRemaining = 1;
    return pushCommand_(readQ_, cmd);
}

bool MicronovaBusModule::queueWrite(uint8_t writeCode,
                                    uint8_t address,
                                    uint8_t value,
                                    uint8_t repeatCount,
                                    uint16_t repeatDelayMs)
{
    MicronovaCommand cmd{};
    cmd.code = writeCode;
    cmd.address = address;
    cmd.value = value;
    cmd.write = true;
    cmd.repeatCount = repeatCount == 0 ? 1 : repeatCount;
    cmd.repeatRemaining = cmd.repeatCount;
    cmd.repeatDelayMs = repeatDelayMs == 0 ? repeatDelayMs_() : repeatDelayMs;
    return pushWrite_(cmd);
}

bool MicronovaBusModule::pollValue(MicronovaRawValue& out)
{
    if (valueQ_.count == 0U) return false;
    out = valueQ_.items[valueQ_.head];
    valueQ_.head = (uint8_t)((valueQ_.head + 1U) % ValueQueueCapacity);
    --valueQ_.count;
    return true;
}

void MicronovaBusModule::setEnableRx_(bool receive)
{
    if (enableRxPin_ < 0) return;
    digitalWrite((uint8_t)enableRxPin_, receive ? HIGH : LOW);
}

void MicronovaBusModule::sendCurrent_(uint32_t nowMs)
{
    if (!begun_) return;
    while (serial_.available() > 0) {
        (void)serial_.read();
    }
    replyLen_ = 0;
    setEnableRx_(false);

    if (current_.write) {
        const uint8_t bytes[4] = {
            current_.code,
            current_.address,
            current_.value,
            MicronovaProtocol::writeChecksum(current_.code, current_.address, current_.value)
        };
        (void)serial_.write(bytes, sizeof(bytes));
        serial_.flush();
        current_.lastSendMs = nowMs;
        if (current_.repeatRemaining > 0) --current_.repeatRemaining;
        state_ = current_.repeatRemaining > 0 ? MicronovaBusState::RepeatDelay : MicronovaBusState::Idle;
        stateTsMs_ = nowMs;
        if (state_ == MicronovaBusState::Idle) finishCurrent_();
        return;
    }

    const uint8_t bytes[2] = {current_.code, current_.address};
    (void)serial_.write(bytes, sizeof(bytes));
    serial_.flush();
    current_.lastSendMs = nowMs;
    setEnableRx_(true);
    state_ = MicronovaBusState::WaitTurnaround;
    stateTsMs_ = nowMs;
}

void MicronovaBusModule::finishCurrent_()
{
    current_ = MicronovaCommand{};
    state_ = MicronovaBusState::Idle;
    stateTsMs_ = millis();
    setEnableRx_(false);
}

void MicronovaBusModule::updateOnline_(bool online)
{
    if (online_ == online) return;
    online_ = online;
    LOGI("Micronova bus %s", online ? "online" : "offline");
}

uint16_t MicronovaBusModule::replyTimeoutMs_() const
{
    return replyTimeoutMsCfg_ > 0 ? (uint16_t)replyTimeoutMsCfg_ : MicronovaProtocol::DefaultReplyTimeoutMs;
}

uint16_t MicronovaBusModule::turnaroundDelayMs_() const
{
    return turnaroundDelayMsCfg_ > 0 ? (uint16_t)turnaroundDelayMsCfg_ : MicronovaProtocol::DefaultTurnaroundDelayMs;
}

uint16_t MicronovaBusModule::repeatDelayMs_() const
{
    return repeatDelayMsCfg_ > 0 ? (uint16_t)repeatDelayMsCfg_ : MicronovaProtocol::DefaultRepeatDelayMs;
}

void MicronovaBusModule::tick(uint32_t nowMs)
{
    if (!begun_) return;

    if (online_ && lastResponseMs_ != 0U && offlineTimeoutMs_ > 0U &&
        (uint32_t)(nowMs - lastResponseMs_) > offlineTimeoutMs_) {
        updateOnline_(false);
    }

    switch (state_) {
        case MicronovaBusState::Idle:
            if (popWrite_(current_) || popRead_(current_)) {
                state_ = MicronovaBusState::Sending;
            }
            break;

        case MicronovaBusState::Sending:
            sendCurrent_(nowMs);
            break;

        case MicronovaBusState::WaitTurnaround:
            if ((uint32_t)(nowMs - stateTsMs_) >= turnaroundDelayMs_()) {
                state_ = MicronovaBusState::WaitingReply;
                stateTsMs_ = nowMs;
            }
            break;

        case MicronovaBusState::WaitingReply:
            while (serial_.available() > 0 && replyLen_ < sizeof(replyBuf_)) {
                const int b = serial_.read();
                if (b >= 0) replyBuf_[replyLen_++] = (uint8_t)b;
            }
            if (replyLen_ >= 2U) {
                const bool valid = MicronovaProtocol::responseMatches(current_.code,
                                                                      current_.address,
                                                                      replyBuf_[0],
                                                                      replyBuf_[1]);
                MicronovaRawValue value{};
                value.readCode = current_.code;
                value.memoryAddress = current_.address;
                value.value = replyBuf_[1];
                value.valid = valid;
                if (valid) {
                    lastResponseMs_ = nowMs;
                    updateOnline_(true);
                    (void)pushValue_(value);
                } else {
                    LOGW("invalid reply code=0x%02X addr=0x%02X chk=0x%02X value=0x%02X",
                         current_.code, current_.address, replyBuf_[0], replyBuf_[1]);
                }
                finishCurrent_();
            } else if ((uint32_t)(nowMs - stateTsMs_) >= replyTimeoutMs_()) {
                updateOnline_(false);
                finishCurrent_();
            }
            break;

        case MicronovaBusState::RepeatDelay:
            if ((uint32_t)(nowMs - current_.lastSendMs) >= current_.repeatDelayMs) {
                state_ = MicronovaBusState::Sending;
            }
            break;
    }
}
