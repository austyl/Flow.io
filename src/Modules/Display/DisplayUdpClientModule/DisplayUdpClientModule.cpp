#include "Modules/Display/DisplayUdpClientModule/DisplayUdpClientModule.h"

#include <string.h>

#include "Board/BoardSerialMap.h"
#include "Core/FirmwareVersion.h"

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::DisplayUdpClientModule)
#include "Core/ModuleLog.h"

void DisplayUdpClientModule::init(ConfigStore& cfgStore, ServiceRegistry& services)
{
    cfgStore.registerVar(tokenVar_);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);

    NextionDriverConfig cfg{};
    cfg.serial = &Board::SerialMap::hmiSerial();
    cfg.rxPin = Board::SerialMap::hmiRxPin();
    cfg.txPin = Board::SerialMap::hmiTxPin();
    cfg.baud = Board::SerialMap::HmiBaud;
    nextion_.setConfig(cfg);
}

void DisplayUdpClientModule::loop()
{
    tick(millis());
    vTaskDelay(pdMS_TO_TICKS(20));
}

bool DisplayUdpClientModule::begin()
{
    if (!nextion_.begin()) return false;
    if (started_) return true;
    if (!udp_.begin(HMI_UDP_PORT)) {
        LOGW("Display UDP begin failed port=%u", (unsigned)HMI_UDP_PORT);
        return false;
    }
    started_ = true;
    LOGI("Display UDP client listening port=%u", (unsigned)HMI_UDP_PORT);
    return true;
}

void DisplayUdpClientModule::tick(uint32_t nowMs)
{
    if (!begin()) return;
    nextion_.tick(nowMs);
    readUdp_(nowMs);
    servicePendingAck_(nowMs);

    if (!wifiConnected_()) {
        linked_ = false;
        return;
    }

    if (!linked_) {
        sendHello_(nowMs);
    } else {
        sendPing_(nowMs);
        pollNextion_();
        if ((uint32_t)(nowMs - lastSeenMs_) > LinkTimeoutMs) {
            handleLinkLost_();
        }
    }
}

void DisplayUdpClientModule::sendHello_(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastHelloMs_) < HelloPeriodMs) return;
    lastHelloMs_ = nowMs;

    HmiUdpHelloPayload payload{};
    payload.tokenCrc = hmiUdpTokenCrc(cfgData_.token);
    payload.displayFw = 1U;
    payload.protoVersion = HMI_UDP_VERSION;

    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_,
                           sizeof(txBuf_),
                           packetLen,
                           HmiUdpMsgType::Hello,
                           txSeq_++,
                           lastRxSeq_,
                           0U,
                           &payload,
                           sizeof(payload))) {
        return;
    }
    IPAddress broadcast(255, 255, 255, 255);
    if (!udp_.beginPacket(broadcast, HMI_UDP_PORT)) return;
    (void)udp_.write(txBuf_, packetLen);
    (void)udp_.endPacket();
}

void DisplayUdpClientModule::sendPing_(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastPingMs_) < PingPeriodMs) return;
    lastPingMs_ = nowMs;
    (void)sendPacket_(HmiUdpMsgType::Ping, nullptr, 0U);
}

void DisplayUdpClientModule::readUdp_(uint32_t nowMs)
{
    int packetSize = udp_.parsePacket();
    while (packetSize > 0) {
        if (packetSize <= (int)sizeof(rxBuf_)) {
            const int len = udp_.read(rxBuf_, sizeof(rxBuf_));
            const HmiUdpHeader* header = nullptr;
            const uint8_t* payload = nullptr;
            if (len > 0 && hmiUdpValidatePacket(rxBuf_, (size_t)len, header, payload)) {
                flowIp_ = udp_.remoteIP();
                flowPort_ = udp_.remotePort();
                handlePacket_(*header, payload, nowMs);
            }
        } else {
            while (udp_.available() > 0) (void)udp_.read();
        }
        packetSize = udp_.parsePacket();
    }
}

void DisplayUdpClientModule::handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs)
{
    lastRxSeq_ = header.seq;
    markSeen_(nowMs);
    if ((header.flags & HMI_UDP_FLAG_ACK_REQUIRED) != 0U) {
        (void)sendAck_(header.seq);
    }
    if ((header.flags & HMI_UDP_FLAG_IS_ACK) != 0U || (HmiUdpMsgType)header.type == HmiUdpMsgType::Ack) {
        lastAck_ = header.ack;
        if (pendingLen_ > 0 && header.ack == pendingSeq_) {
            pendingLen_ = 0;
            pendingAttempts_ = 0;
        }
        return;
    }

    switch ((HmiUdpMsgType)header.type) {
        case HmiUdpMsgType::Welcome: {
            if (header.len != sizeof(HmiUdpWelcomePayload) || !payload) return;
            const auto* welcome = reinterpret_cast<const HmiUdpWelcomePayload*>(payload);
            linked_ = welcome->accepted != 0U;
            lostShown_ = false;
            if (linked_) LOGI("Display linked to FlowIO");
            break;
        }
        case HmiUdpMsgType::Pong:
            break;
        case HmiUdpMsgType::HomeText: {
            if (header.len != sizeof(HmiUdpHomeTextPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpHomeTextPayload*>(payload);
            (void)nextion_.publishHomeText((HmiHomeTextField)p->field, p->text);
            break;
        }
        case HmiUdpMsgType::HomeGauge: {
            if (header.len != sizeof(HmiUdpHomeGaugePayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpHomeGaugePayload*>(payload);
            (void)nextion_.publishHomeGaugePercent((HmiHomeGaugeField)p->field, p->percent);
            break;
        }
        case HmiUdpMsgType::HomeStateBits: {
            if (header.len != sizeof(HmiUdpStateBitsPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpStateBitsPayload*>(payload);
            (void)nextion_.publishHomeStateBits(p->stateBits);
            break;
        }
        case HmiUdpMsgType::HomeAlarmBits: {
            if (header.len != sizeof(HmiUdpAlarmBitsPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpAlarmBitsPayload*>(payload);
            (void)nextion_.publishHomeAlarmBits(p->alarmBits);
            break;
        }
        case HmiUdpMsgType::ConfigStart: {
            if (header.len != sizeof(HmiUdpConfigStartPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigStartPayload*>(payload);
            configView_ = ConfigMenuView{};
            configView_.pageIndex = p->page > 0U ? (uint8_t)(p->page - 1U) : 0U;
            configView_.pageCount = p->pageCount;
            copyText_(configView_.breadcrumb, sizeof(configView_.breadcrumb), p->title);
            break;
        }
        case HmiUdpMsgType::ConfigRow: {
            if (header.len != sizeof(HmiUdpConfigRowPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigRowPayload*>(payload);
            if (p->row >= ConfigMenuModel::RowsPerPage) return;
            ConfigMenuRowView& row = configView_.rows[p->row];
            row.visible = (p->flags & HMI_UDP_CONFIG_ROW_VISIBLE) != 0U;
            row.valueVisible = (p->flags & HMI_UDP_CONFIG_ROW_VALUE_VISIBLE) != 0U;
            row.editable = (p->flags & HMI_UDP_CONFIG_ROW_EDITABLE) != 0U;
            row.dirty = (p->flags & HMI_UDP_CONFIG_ROW_DIRTY) != 0U;
            row.canEnter = (p->flags & HMI_UDP_CONFIG_ROW_CAN_ENTER) != 0U;
            row.canEdit = (p->flags & HMI_UDP_CONFIG_ROW_CAN_EDIT) != 0U;
            configView_.mode = (p->flags & HMI_UDP_CONFIG_MODE_EDIT) != 0U ? ConfigMenuMode::Edit : ConfigMenuMode::Browse;
            row.widget = (ConfigMenuWidget)p->widget;
            copyText_(row.label, sizeof(row.label), p->label);
            copyText_(row.value, sizeof(row.value), p->value);
            (void)nextion_.refreshConfigMenuValues(configView_);
            break;
        }
        case HmiUdpMsgType::ConfigEnd: {
            if (header.len != sizeof(HmiUdpConfigEndPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigEndPayload*>(payload);
            configView_.rowCountOnPage = p->rowCount;
            (void)nextion_.renderConfigMenu(configView_);
            break;
        }
        case HmiUdpMsgType::RtcWrite: {
            if (header.len != sizeof(HmiUdpRtcPayload) || !payload) return;
            HmiRtcDateTime rtc{};
            hmiUdpPayloadToRtc(*reinterpret_cast<const HmiUdpRtcPayload*>(payload), rtc);
            (void)nextion_.writeRtc(rtc);
            break;
        }
        case HmiUdpMsgType::FullRefresh:
            break;
        default:
            break;
    }
}

bool DisplayUdpClientModule::sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags)
{
    if (!started_ || !wifiConnected_()) return false;
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_, sizeof(txBuf_), packetLen, type, txSeq_++, lastRxSeq_, flags, payload, payloadLen)) {
        return false;
    }
    if (!udp_.beginPacket(flowIp_, flowPort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

bool DisplayUdpClientModule::sendAck_(uint16_t seq)
{
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_,
                           sizeof(txBuf_),
                           packetLen,
                           HmiUdpMsgType::Ack,
                           txSeq_++,
                           seq,
                           HMI_UDP_FLAG_IS_ACK,
                           nullptr,
                           0U)) {
        return false;
    }
    if (!udp_.beginPacket(flowIp_, flowPort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

void DisplayUdpClientModule::pollNextion_()
{
    if (pendingLen_ > 0) return;
    HmiEvent event{};
    if (nextion_.pollEvent(event)) {
        (void)sendEvent_(event);
    }
}

bool DisplayUdpClientModule::sendEvent_(const HmiEvent& event)
{
    HmiUdpEventPayload payload{};
    hmiUdpEventToPayload(event, payload);
    pendingSeq_ = txSeq_;
    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(pendingBuf_,
                           sizeof(pendingBuf_),
                           packetLen,
                           HmiUdpMsgType::HmiEvent,
                           txSeq_++,
                           lastRxSeq_,
                           HMI_UDP_FLAG_ACK_REQUIRED,
                           &payload,
                           sizeof(payload))) {
        pendingSeq_ = 0;
        return false;
    }
    pendingLen_ = packetLen;
    pendingAttempts_ = 0;
    pendingLastSendMs_ = 0;
    servicePendingAck_(millis());
    return pendingLen_ > 0;
}

void DisplayUdpClientModule::servicePendingAck_(uint32_t nowMs)
{
    if (pendingLen_ == 0 || !linked_) return;
    if (pendingAttempts_ >= AckMaxAttempts) {
        pendingLen_ = 0;
        pendingAttempts_ = 0;
        return;
    }
    if (pendingAttempts_ > 0 && (uint32_t)(nowMs - pendingLastSendMs_) < AckRetryMs) return;
    if (!udp_.beginPacket(flowIp_, flowPort_)) return;
    const size_t written = udp_.write(pendingBuf_, pendingLen_);
    if (written == pendingLen_ && udp_.endPacket() == 1) {
        ++pendingAttempts_;
        pendingLastSendMs_ = nowMs;
    }
}

void DisplayUdpClientModule::markSeen_(uint32_t nowMs)
{
    lastSeenMs_ = nowMs;
}

void DisplayUdpClientModule::handleLinkLost_()
{
    linked_ = false;
    pendingLen_ = 0;
    if (!lostShown_) {
        lostShown_ = true;
        (void)nextion_.publishHomeText(HmiHomeTextField::ErrorMessage, "Connexion Flow.io perdue");
        LOGW("Display lost FlowIO link");
    }
}

bool DisplayUdpClientModule::wifiConnected_() const
{
    return wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx);
}

void DisplayUdpClientModule::copyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!in) return;
    size_t i = 0;
    while (i + 1 < outLen && in[i] != '\0') {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}
