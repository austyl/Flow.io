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
    cfg.homePageId = 1U;
    cfg.configPageId = 2U;
    cfg.homePageAliasId = 0U;
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
    if (!wifiConnected_()) return true;

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

    if (!wifiConnected_()) {
        linked_ = false;
        return;
    }
    if (!started_) return;

    readUdp_(nowMs);
    servicePendingAck_(nowMs);
    serviceConfigRendering_(nowMs);
    if (inputLocked_ && (int32_t)(nowMs - inputLockedAtMs_) > (int32_t)InputLockMaxMs) {
        LOGW("Display Nextion touch lock watchdog expired");
        configBatchActive_ = false;
        configRenderPending_ = false;
        configValuesPending_ = false;
        setInputLocked_(false, "watchdog", nowMs);
    }

    if (!linked_) {
        sendHello_(nowMs);
    } else {
        sendPing_(nowMs);
        servicePageProbe_(nowMs);
        pollNextion_();
        serviceEventTx_();
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
    const HmiUdpMsgType msgType = (HmiUdpMsgType)header.type;
    LOGI("FlowIO -> Display msg=%s seq=%u ack=%u flags=0x%02X len=%u",
         msgTypeName_(msgType),
         (unsigned)header.seq,
         (unsigned)header.ack,
         (unsigned)header.flags,
         (unsigned)header.len);
    if ((header.flags & HMI_UDP_FLAG_ACK_REQUIRED) != 0U) {
        (void)sendAck_(header.seq);
    }
    if ((header.flags & HMI_UDP_FLAG_IS_ACK) != 0U || msgType == HmiUdpMsgType::Ack) {
        lastAck_ = header.ack;
        if (pendingLen_ > 0 && header.ack == pendingSeq_) {
            LOGI("Display event ACK received seq=%u attempts=%u", (unsigned)pendingSeq_, (unsigned)pendingAttempts_);
            pendingLen_ = 0;
            pendingAttempts_ = 0;
        }
        return;
    }

    switch (msgType) {
        case HmiUdpMsgType::Welcome: {
            if (header.len != sizeof(HmiUdpWelcomePayload) || !payload) return;
            const auto* welcome = reinterpret_cast<const HmiUdpWelcomePayload*>(payload);
            linked_ = welcome->accepted != 0U;
            lostShown_ = false;
            if (linked_) {
                LOGI("Display linked to FlowIO proto=%u fw=%u", (unsigned)welcome->protoVersion, (unsigned)welcome->flowFw);
                requestFullRefresh_("welcome");
            }
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
            configView_.canHome = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_HOME) != 0U;
            configView_.canBack = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_BACK) != 0U;
            configView_.canValidate = (p->flags & HMI_UDP_CONFIG_VIEW_CAN_VALIDATE) != 0U;
            configView_.isHome = (p->flags & HMI_UDP_CONFIG_VIEW_IS_HOME) != 0U;
            copyText_(configView_.breadcrumb, sizeof(configView_.breadcrumb), p->title);
            configBatchActive_ = true;
            configRenderPending_ = false;
            configValuesPending_ = false;
            setInputLocked_(true, "config-batch", nowMs);
            const bool loadingOk = nextion_.showConfigLoading(configView_.breadcrumb);
            LOGI("Display config batch start page=%u/%u flags=0x%02X canBack=%u title='%s' loading=%d",
                 (unsigned)p->page,
                 (unsigned)p->pageCount,
                 (unsigned)p->flags,
                 configView_.canBack ? 1U : 0U,
                 configView_.breadcrumb,
                 loadingOk ? 1 : 0);
            logDisplayState_("config-start", true);
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
            LOGI("Display config row row=%u widget=%s flags=0x%02X label='%s' value='%s'",
                 (unsigned)p->row,
                 widgetName_(row.widget),
                 (unsigned)p->flags,
                 row.label,
                 row.value);
            if (!configBatchActive_) {
                scheduleConfigValuesRefresh_(nowMs, "row-update");
                logDisplayState_("config-row-update", true);
            }
            break;
        }
        case HmiUdpMsgType::ConfigEnd: {
            if (header.len != sizeof(HmiUdpConfigEndPayload) || !payload) return;
            const auto* p = reinterpret_cast<const HmiUdpConfigEndPayload*>(payload);
            configView_.rowCountOnPage = p->rowCount;
            configBatchActive_ = false;
            logDisplayState_("config-end", true);
            scheduleConfigRender_(nowMs, "config-end");
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
    uint8_t drained = 0;
    HmiEvent event{};
    while (drained < 4U && nextion_.pollEvent(event)) {
        logEvent_("Nextion -> Display", event);
        if (event.type == HmiEventType::Home ||
            event.type == HmiEventType::ConfigExit ||
            (event.type == HmiEventType::Command && event.command == HmiCommandId::HomeSyncRequest)) {
            requestFullRefresh_("nextion-home", true);
        }
        if (inputLocked_) {
            LOGI("Display drops Nextion event while input locked event=%s command=%s",
                 eventTypeName_(event.type),
                 commandName_(event.command));
            ++drained;
            continue;
        }
        (void)enqueueEvent_(event);
        ++drained;
    }
}

void DisplayUdpClientModule::serviceEventTx_()
{
    if (pendingLen_ > 0) return;
    HmiEvent event{};
    if (!dequeueEvent_(event)) return;
    (void)sendEvent_(event);
}

bool DisplayUdpClientModule::enqueueEvent_(const HmiEvent& event)
{
    const uint8_t next = (uint8_t)((eventHead_ + 1U) % NEXTION_EVENT_QUEUE_SIZE);
    if (next == eventTail_) {
        LOGW("Display Nextion event queue full drop type=%s command=%s",
             eventTypeName_(event.type),
             commandName_(event.command));
        return false;
    }
    eventQueue_[eventHead_] = event;
    eventHead_ = next;
    return true;
}

bool DisplayUdpClientModule::dequeueEvent_(HmiEvent& out)
{
    out = HmiEvent{};
    if (eventHead_ == eventTail_) return false;
    out = eventQueue_[eventTail_];
    eventTail_ = (uint8_t)((eventTail_ + 1U) % NEXTION_EVENT_QUEUE_SIZE);
    return true;
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
    logEvent_("Display -> FlowIO", event);
    LOGI("Display queued HmiEvent seq=%u len=%u", (unsigned)pendingSeq_, (unsigned)packetLen);
    pendingLen_ = packetLen;
    pendingAttempts_ = 0;
    pendingLastSendMs_ = 0;
    servicePendingAck_(millis());
    return pendingLen_ > 0;
}

void DisplayUdpClientModule::requestFullRefresh_(const char* reason, bool force)
{
    if (!linked_) return;
    const uint32_t now = millis();
    if (!force &&
        lastHomeRefreshRequestMs_ != 0U &&
        (uint32_t)(now - lastHomeRefreshRequestMs_) < HomeRefreshThrottleMs) {
        return;
    }
    lastHomeRefreshRequestMs_ = now;
    const bool ok = sendPacket_(HmiUdpMsgType::FullRefresh, nullptr, 0U);
    LOGI("Display requested FlowIO full refresh reason=%s ok=%d",
         reason ? reason : "unknown",
         ok ? 1 : 0);
}

void DisplayUdpClientModule::servicePageProbe_(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastPageProbeMs_) >= PageProbePeriodMs) {
        lastPageProbeMs_ = nowMs;
        const bool ok = nextion_.requestPageReport();
        if (!ok) {
            LOGW("Display Nextion page probe send failed");
        }
    }

    uint8_t page = 0;
    if (nextion_.currentPage(page)) {
        logDisplayState_("page-probe");
        if (nextion_.isHomePage()) {
            requestFullRefresh_("home-page-probe");
        }
    }
}

void DisplayUdpClientModule::scheduleConfigRender_(uint32_t nowMs, const char* reason)
{
    setInputLocked_(true, reason, nowMs);
    configRenderPending_ = true;
    configRenderRemaining_ = ConfigRenderPasses;
    configRenderDueMs_ = nowMs + ConfigRenderDelayMs;
    LOGI("Display config render scheduled reason=%s rows=%u passes=%u",
         reason ? reason : "unknown",
         (unsigned)configView_.rowCountOnPage,
         (unsigned)configRenderRemaining_);
    logDisplayState_("config-render-scheduled", true);
}

void DisplayUdpClientModule::scheduleConfigValuesRefresh_(uint32_t nowMs, const char* reason)
{
    configValuesPending_ = true;
    configValuesRemaining_ = ConfigValuesPasses;
    configValuesDueMs_ = nowMs + ConfigValuesDelayMs;
    LOGI("Display config values refresh scheduled reason=%s passes=%u",
         reason ? reason : "unknown",
         (unsigned)configValuesRemaining_);
}

void DisplayUdpClientModule::serviceConfigRendering_(uint32_t nowMs)
{
    if (configRenderPending_ && (int32_t)(nowMs - configRenderDueMs_) >= 0) {
        const bool ok = nextion_.renderConfigMenu(configView_);
        LOGI("Display config render pass ok=%d remaining=%u page=%u rows=%u",
             ok ? 1 : 0,
             (unsigned)configRenderRemaining_,
             (unsigned)(configView_.pageIndex + 1U),
             (unsigned)configView_.rowCountOnPage);
        logDisplayState_("config-render-pass");
        if (configRenderRemaining_ > 0U) {
            --configRenderRemaining_;
        }
        if (configRenderRemaining_ == 0U) {
            configRenderPending_ = false;
            setInputLocked_(false, "config-render-complete", nowMs);
        } else {
            configRenderDueMs_ = nowMs + ConfigRenderRetryMs;
        }
    }

    if (configValuesPending_ && (int32_t)(nowMs - configValuesDueMs_) >= 0) {
        const bool ok = nextion_.refreshConfigMenuValues(configView_);
        LOGI("Display config values refresh pass ok=%d remaining=%u",
             ok ? 1 : 0,
             (unsigned)configValuesRemaining_);
        if (configValuesRemaining_ > 0U) {
            --configValuesRemaining_;
        }
        if (configValuesRemaining_ == 0U) {
            configValuesPending_ = false;
        } else {
            configValuesDueMs_ = nowMs + ConfigRenderRetryMs;
        }
    }
}

void DisplayUdpClientModule::setInputLocked_(bool locked, const char* reason, uint32_t nowMs)
{
    if (inputLocked_ == locked) {
        if (locked) {
            inputLockedAtMs_ = nowMs;
        }
        return;
    }
    inputLocked_ = locked;
    if (locked) {
        inputLockedAtMs_ = nowMs;
    }
    const bool ok = nextion_.setTouchEnabled(!locked);
    LOGI("Display Nextion touch %s reason=%s ok=%d",
         locked ? "locked" : "unlocked",
         reason ? reason : "unknown",
         ok ? 1 : 0);
    logDisplayState_(reason ? reason : "touch-lock", true);
}

void DisplayUdpClientModule::logDisplayState_(const char* reason, bool force)
{
    uint8_t nextionPage = 0xFFU;
    bool pageKnown = nextion_.currentPage(nextionPage);
    uint8_t logicalPage = 0xFFU;
    if (pageKnown) {
        if (nextion_.isHomePage()) {
            logicalPage = 0U;
        } else if (nextion_.isConfigPage()) {
            logicalPage = 1U;
        }
    }

    const uint8_t configPage = (uint8_t)(configView_.pageIndex + 1U);
    const bool changed =
        force ||
        nextionPage != lastLoggedNextionPage_ ||
        logicalPage != lastLoggedLogicalPage_ ||
        configPage != lastLoggedConfigPage_ ||
        configView_.pageCount != lastLoggedConfigPageCount_ ||
        configView_.rowCountOnPage != lastLoggedConfigRows_ ||
        configView_.mode != lastLoggedConfigMode_ ||
        strncmp(configView_.breadcrumb, lastLoggedConfigPath_, sizeof(lastLoggedConfigPath_)) != 0;
    if (!changed) return;

    lastLoggedNextionPage_ = nextionPage;
    lastLoggedLogicalPage_ = logicalPage;
    lastLoggedConfigPage_ = configPage;
    lastLoggedConfigPageCount_ = configView_.pageCount;
    lastLoggedConfigRows_ = configView_.rowCountOnPage;
    lastLoggedConfigMode_ = configView_.mode;
    copyText_(lastLoggedConfigPath_, sizeof(lastLoggedConfigPath_), configView_.breadcrumb);

    LOGI("Display state reason=%s nextionPage=%s%u logicalPage=%u(%s) configPage=%u/%u mode=%s rows=%u submenu='%s' inputLocked=%u batch=%u renderPending=%u valuesPending=%u",
         reason ? reason : "state",
         pageKnown ? "" : "?",
         (unsigned)nextionPage,
         (unsigned)logicalPage,
         logicalPage == 0U ? "Home" : (logicalPage == 1U ? "Config" : "Other"),
         (unsigned)configPage,
         (unsigned)configView_.pageCount,
         menuModeName_(configView_.mode),
         (unsigned)configView_.rowCountOnPage,
         configView_.breadcrumb,
         inputLocked_ ? 1U : 0U,
         configBatchActive_ ? 1U : 0U,
         configRenderPending_ ? 1U : 0U,
         configValuesPending_ ? 1U : 0U);
}

void DisplayUdpClientModule::logEvent_(const char* prefix, const HmiEvent& event) const
{
    LOGI("%s event=%s command=%s row=%u value=%u dir=%d slider=%.2f text='%s'",
         prefix ? prefix : "HMI",
         eventTypeName_(event.type),
         commandName_(event.command),
         (unsigned)event.row,
         (unsigned)event.value,
         (int)event.direction,
         (double)event.sliderValue,
         event.text);
}

void DisplayUdpClientModule::servicePendingAck_(uint32_t nowMs)
{
    if (pendingLen_ == 0 || !linked_) return;
    if (pendingAttempts_ >= AckMaxAttempts) {
        LOGW("Display event ACK timeout seq=%u attempts=%u",
             (unsigned)pendingSeq_,
             (unsigned)pendingAttempts_);
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
        LOGI("Display sent HmiEvent seq=%u attempt=%u len=%u",
             (unsigned)pendingSeq_,
             (unsigned)pendingAttempts_,
             (unsigned)pendingLen_);
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
    eventHead_ = 0;
    eventTail_ = 0;
    setInputLocked_(false, "link-lost", millis());
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

const char* DisplayUdpClientModule::msgTypeName_(HmiUdpMsgType type)
{
    switch (type) {
        case HmiUdpMsgType::Hello: return "Hello";
        case HmiUdpMsgType::Welcome: return "Welcome";
        case HmiUdpMsgType::Ping: return "Ping";
        case HmiUdpMsgType::Pong: return "Pong";
        case HmiUdpMsgType::Ack: return "Ack";
        case HmiUdpMsgType::HomeText: return "HomeText";
        case HmiUdpMsgType::HomeGauge: return "HomeGauge";
        case HmiUdpMsgType::HomeStateBits: return "HomeStateBits";
        case HmiUdpMsgType::HomeAlarmBits: return "HomeAlarmBits";
        case HmiUdpMsgType::FullRefresh: return "FullRefresh";
        case HmiUdpMsgType::HmiEvent: return "HmiEvent";
        case HmiUdpMsgType::ConfigStart: return "ConfigStart";
        case HmiUdpMsgType::ConfigRow: return "ConfigRow";
        case HmiUdpMsgType::ConfigEnd: return "ConfigEnd";
        case HmiUdpMsgType::ConfigValues: return "ConfigValues";
        case HmiUdpMsgType::RtcReadRequest: return "RtcReadRequest";
        case HmiUdpMsgType::RtcReadResponse: return "RtcReadResponse";
        case HmiUdpMsgType::RtcWrite: return "RtcWrite";
        case HmiUdpMsgType::Error: return "Error";
        default: return "?";
    }
}

const char* DisplayUdpClientModule::eventTypeName_(HmiEventType type)
{
    switch (type) {
        case HmiEventType::None: return "None";
        case HmiEventType::Home: return "Home";
        case HmiEventType::Back: return "Back";
        case HmiEventType::Validate: return "Validate";
        case HmiEventType::NextPage: return "NextPage";
        case HmiEventType::PrevPage: return "PrevPage";
        case HmiEventType::RowActivate: return "RowActivate";
        case HmiEventType::RowToggle: return "RowToggle";
        case HmiEventType::RowCycle: return "RowCycle";
        case HmiEventType::RowSetText: return "RowSetText";
        case HmiEventType::RowSetSlider: return "RowSetSlider";
        case HmiEventType::RowEdit: return "RowEdit";
        case HmiEventType::Command: return "Command";
        case HmiEventType::ConfigEnter: return "ConfigEnter";
        case HmiEventType::ConfigExit: return "ConfigExit";
        default: return "?";
    }
}

const char* DisplayUdpClientModule::commandName_(HmiCommandId command)
{
    switch (command) {
        case HmiCommandId::None: return "None";
        case HmiCommandId::HomeFiltrationSet: return "HomeFiltrationSet";
        case HmiCommandId::HomeAutoModeSet: return "HomeAutoModeSet";
        case HmiCommandId::HomeSyncRequest: return "HomeSyncRequest";
        case HmiCommandId::HomeFiltrationToggle: return "HomeFiltrationToggle";
        case HmiCommandId::HomeAutoModeToggle: return "HomeAutoModeToggle";
        case HmiCommandId::HomeOrpAutoModeToggle: return "HomeOrpAutoModeToggle";
        case HmiCommandId::HomePhAutoModeToggle: return "HomePhAutoModeToggle";
        case HmiCommandId::HomeWinterModeToggle: return "HomeWinterModeToggle";
        case HmiCommandId::HomeLightsToggle: return "HomeLightsToggle";
        case HmiCommandId::HomeRobotToggle: return "HomeRobotToggle";
        case HmiCommandId::HomeConfigOpen: return "HomeConfigOpen";
        case HmiCommandId::HomePhPumpSet: return "HomePhPumpSet";
        case HmiCommandId::HomeOrpPumpSet: return "HomeOrpPumpSet";
        case HmiCommandId::HomePhPumpToggle: return "HomePhPumpToggle";
        case HmiCommandId::HomeOrpPumpToggle: return "HomeOrpPumpToggle";
        default: return "?";
    }
}

const char* DisplayUdpClientModule::menuModeName_(ConfigMenuMode mode)
{
    switch (mode) {
        case ConfigMenuMode::Browse: return "Browse";
        case ConfigMenuMode::Edit: return "Edit";
        default: return "?";
    }
}

const char* DisplayUdpClientModule::widgetName_(ConfigMenuWidget widget)
{
    switch (widget) {
        case ConfigMenuWidget::Text: return "Text";
        case ConfigMenuWidget::Switch: return "Switch";
        case ConfigMenuWidget::Select: return "Select";
        case ConfigMenuWidget::Slider: return "Slider";
        default: return "?";
    }
}
