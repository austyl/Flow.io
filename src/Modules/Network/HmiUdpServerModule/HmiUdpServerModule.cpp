#include "Modules/Network/HmiUdpServerModule/HmiUdpServerModule.h"

#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HmiUdpServerModule)
#include "Core/ModuleLog.h"

void HmiUdpServerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfg.registerVar(tokenVar_);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
}

bool HmiUdpServerModule::begin()
{
    if (started_) return true;
    if (!udp_.begin(HMI_UDP_PORT)) {
        LOGW("HMI UDP begin failed port=%u", (unsigned)HMI_UDP_PORT);
        return false;
    }
    started_ = true;
    LOGI("HMI UDP server listening port=%u", (unsigned)HMI_UDP_PORT);
    return true;
}

void HmiUdpServerModule::tick(uint32_t nowMs)
{
    if (!begin()) return;
    readUdp_(nowMs);
    if (displayOnline_ && (uint32_t)(nowMs - lastSeenMs_) > OfflineTimeoutMs) {
        displayOnline_ = false;
        LOGW("HMI UDP display offline");
    }
}

bool HmiUdpServerModule::consumeFullRefreshRequested()
{
    const bool requested = fullRefreshRequested_;
    fullRefreshRequested_ = false;
    return requested;
}

bool HmiUdpServerModule::sendHomeText(HmiHomeTextField field, const char* text)
{
    HmiUdpHomeTextPayload payload{};
    payload.field = (uint8_t)field;
    copyText_(payload.text, sizeof(payload.text), text);
    return sendPacket_(HmiUdpMsgType::HomeText, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendHomeGauge(HmiHomeGaugeField field, uint16_t percent)
{
    HmiUdpHomeGaugePayload payload{};
    payload.field = (uint8_t)field;
    payload.percent = percent;
    return sendPacket_(HmiUdpMsgType::HomeGauge, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendHomeStateBits(uint32_t stateBits)
{
    HmiUdpStateBitsPayload payload{};
    payload.stateBits = stateBits;
    return sendPacket_(HmiUdpMsgType::HomeStateBits, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendHomeAlarmBits(uint32_t alarmBits)
{
    HmiUdpAlarmBitsPayload payload{};
    payload.alarmBits = alarmBits;
    return sendPacket_(HmiUdpMsgType::HomeAlarmBits, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendConfigStart(const ConfigMenuView& view)
{
    HmiUdpConfigStartPayload payload{};
    payload.page = (uint8_t)(view.pageIndex + 1U);
    payload.pageCount = view.pageCount;
    copyText_(payload.title, sizeof(payload.title), view.breadcrumb);
    return sendPacket_(HmiUdpMsgType::ConfigStart, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendConfigRow(uint8_t row, const ConfigMenuRowView& viewRow, ConfigMenuMode mode)
{
    HmiUdpConfigRowPayload payload{};
    payload.row = row;
    payload.widget = (uint8_t)viewRow.widget;
    if (viewRow.visible) payload.flags |= HMI_UDP_CONFIG_ROW_VISIBLE;
    if (viewRow.valueVisible) payload.flags |= HMI_UDP_CONFIG_ROW_VALUE_VISIBLE;
    if (viewRow.editable) payload.flags |= HMI_UDP_CONFIG_ROW_EDITABLE;
    if (viewRow.dirty) payload.flags |= HMI_UDP_CONFIG_ROW_DIRTY;
    if (viewRow.canEnter) payload.flags |= HMI_UDP_CONFIG_ROW_CAN_ENTER;
    if (viewRow.canEdit) payload.flags |= HMI_UDP_CONFIG_ROW_CAN_EDIT;
    if (mode == ConfigMenuMode::Edit) payload.flags |= HMI_UDP_CONFIG_MODE_EDIT;
    copyText_(payload.label, sizeof(payload.label), viewRow.label);
    copyText_(payload.value, sizeof(payload.value), viewRow.value);
    return sendPacket_(HmiUdpMsgType::ConfigRow, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendConfigEnd(uint8_t rowCount)
{
    HmiUdpConfigEndPayload payload{};
    payload.rowCount = rowCount;
    return sendPacket_(HmiUdpMsgType::ConfigEnd, &payload, sizeof(payload));
}

bool HmiUdpServerModule::sendRtcWrite(const HmiRtcDateTime& value)
{
    HmiUdpRtcPayload payload{};
    hmiUdpRtcToPayload(value, payload);
    return sendPacket_(HmiUdpMsgType::RtcWrite, &payload, sizeof(payload), HMI_UDP_FLAG_ACK_REQUIRED);
}

bool HmiUdpServerModule::requestRtcRead()
{
    return sendPacket_(HmiUdpMsgType::RtcReadRequest, nullptr, 0U, HMI_UDP_FLAG_ACK_REQUIRED);
}

bool HmiUdpServerModule::pollEvent(HmiEvent& out)
{
    out = HmiEvent{};
    if (eventHead_ == eventTail_) return false;
    out = eventQueue_[eventTail_];
    eventTail_ = (uint8_t)((eventTail_ + 1U) % HMI_UDP_EVENT_QUEUE_SIZE);
    return true;
}

bool HmiUdpServerModule::sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags)
{
    if (!started_ || !displayOnline_ || !wifiConnected_()) return false;

    size_t packetLen = 0;
    if (!hmiUdpBuildPacket(txBuf_,
                           sizeof(txBuf_),
                           packetLen,
                           type,
                           txSeq_++,
                           lastRxSeq_,
                           flags,
                           payload,
                           payloadLen)) {
        return false;
    }
    if (!udp_.beginPacket(remoteIp_, remotePort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

bool HmiUdpServerModule::sendAck_(uint16_t seq)
{
    if (!started_) return false;
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
    if (!udp_.beginPacket(remoteIp_, remotePort_)) return false;
    const size_t written = udp_.write(txBuf_, packetLen);
    return written == packetLen && udp_.endPacket() == 1;
}

void HmiUdpServerModule::readUdp_(uint32_t nowMs)
{
    int packetSize = udp_.parsePacket();
    while (packetSize > 0) {
        if (packetSize <= (int)sizeof(rxBuf_)) {
            const int len = udp_.read(rxBuf_, sizeof(rxBuf_));
            const HmiUdpHeader* header = nullptr;
            const uint8_t* payload = nullptr;
            if (len > 0 && hmiUdpValidatePacket(rxBuf_, (size_t)len, header, payload)) {
                remoteIp_ = udp_.remoteIP();
                remotePort_ = udp_.remotePort();
                handlePacket_(*header, payload, nowMs);
            }
        } else {
            while (udp_.available() > 0) (void)udp_.read();
        }
        packetSize = udp_.parsePacket();
    }
}

void HmiUdpServerModule::handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs)
{
    lastRxSeq_ = header.seq;
    markRemote_(nowMs);

    const HmiUdpMsgType type = (HmiUdpMsgType)header.type;
    if ((header.flags & HMI_UDP_FLAG_ACK_REQUIRED) != 0U) {
        (void)sendAck_(header.seq);
    }

    switch (type) {
        case HmiUdpMsgType::Hello: {
            if (header.len != sizeof(HmiUdpHelloPayload)) return;
            const auto* hello = reinterpret_cast<const HmiUdpHelloPayload*>(payload);
            HmiUdpWelcomePayload welcome{};
            welcome.flowFw = 1U;
            welcome.protoVersion = HMI_UDP_VERSION;
            welcome.accepted = tokenAccepted_(hello ? hello->tokenCrc : 0U) ? 1U : 0U;
            if (welcome.accepted == 0U) {
                (void)sendPacket_(HmiUdpMsgType::Welcome, &welcome, sizeof(welcome));
                displayOnline_ = false;
                return;
            }
            displayOnline_ = true;
            fullRefreshRequested_ = true;
            (void)sendPacket_(HmiUdpMsgType::Welcome, &welcome, sizeof(welcome));
            break;
        }
        case HmiUdpMsgType::Ping:
            (void)sendPacket_(HmiUdpMsgType::Pong, nullptr, 0U);
            break;
        case HmiUdpMsgType::HmiEvent: {
            if (header.len != sizeof(HmiUdpEventPayload) || !payload) return;
            if (hasLastEventSeq_ && header.seq == lastEventSeq_) return;
            HmiEvent event{};
            hmiUdpPayloadToEvent(*reinterpret_cast<const HmiUdpEventPayload*>(payload), event);
            if (pushEvent_(event)) {
                lastEventSeq_ = header.seq;
                hasLastEventSeq_ = true;
            }
            break;
        }
        default:
            break;
    }
}

void HmiUdpServerModule::markRemote_(uint32_t nowMs)
{
    if (!displayOnline_) {
        LOGI("HMI UDP display linked");
    }
    displayOnline_ = true;
    lastSeenMs_ = nowMs;
}

bool HmiUdpServerModule::pushEvent_(const HmiEvent& event)
{
    const uint8_t next = (uint8_t)((eventHead_ + 1U) % HMI_UDP_EVENT_QUEUE_SIZE);
    if (next == eventTail_) return false;
    eventQueue_[eventHead_] = event;
    eventHead_ = next;
    return true;
}

bool HmiUdpServerModule::wifiConnected_() const
{
    return wifiSvc_ && wifiSvc_->isConnected && wifiSvc_->isConnected(wifiSvc_->ctx);
}

bool HmiUdpServerModule::tokenAccepted_(uint32_t tokenCrc) const
{
    if (cfgData_.token[0] == '\0') return true;
    return tokenCrc != 0U && tokenCrc == hmiUdpTokenCrc(cfgData_.token);
}

void HmiUdpServerModule::copyText_(char* out, size_t outLen, const char* in)
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
