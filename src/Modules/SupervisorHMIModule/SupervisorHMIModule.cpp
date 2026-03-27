/**
 * @file SupervisorHMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"

#include "Board/BoardSpec.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

#include "App/FirmwareProfile.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

namespace {
static constexpr uint32_t kFwPollMs = 500U;
static constexpr uint32_t kStartupSplashHoldMs = 5000U;
static constexpr uint32_t kStartupBacklightForceOnMs = 3600000U;
static constexpr uint32_t kButtonBootGuardMs = 8000U;
static constexpr uint32_t kButtonArmHighStableMs = 500U;
static constexpr uint32_t kPageRotateMs = 10000U;

const SupervisorBoardSpec& supervisorBoardSpec_(const BoardSpec& board)
{
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            33,
            14,
            15,
            4,
            5,
            19,
            18,
            true,
            false,
            40000000U,
            80
        },
        {
            36,
            120,
            true,
            23,
            40
        },
        {
            25,
            26,
            13,
            115200U
        }
    };
    const SupervisorBoardSpec* cfg = boardSupervisorConfig(board);
    return cfg ? *cfg : kFallback;
}

} // namespace

SupervisorHMIModule::SupervisorHMIModule(const BoardSpec& board, const SupervisorRuntimeOptions& runtime)
    : driverCfg_(makeDriverConfig_(board)),
      driver_(driverCfg_)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    pirPin_ = boardCfg.inputs.pirPin;
    wifiResetPin_ = boardCfg.inputs.wifiResetPin;
    pirTimeoutMs_ = runtime.pirTimeoutMs;
    pirDebounceMs_ = boardCfg.inputs.pirDebounceMs;
    pirActiveHigh_ = boardCfg.inputs.pirActiveHigh;
    wifiResetHoldMs_ = runtime.wifiResetHoldMs;
    wifiResetDebounceMs_ = boardCfg.inputs.wifiResetDebounceMs;
}

St7789SupervisorDriverConfig SupervisorHMIModule::makeDriverConfig_(const BoardSpec& board)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    St7789SupervisorDriverConfig cfg{};
    cfg.resX = boardCfg.display.resX;
    cfg.resY = boardCfg.display.resY;
    cfg.rotation = boardCfg.display.rotation;
    cfg.colStart = boardCfg.display.colStart;
    cfg.rowStart = boardCfg.display.rowStart;
    cfg.backlightPin = boardCfg.display.backlightPin;
    cfg.csPin = boardCfg.display.csPin;
    cfg.dcPin = boardCfg.display.dcPin;
    cfg.rstPin = boardCfg.display.rstPin;
    cfg.mosiPin = boardCfg.display.mosiPin;
    cfg.sclkPin = boardCfg.display.sclkPin;
    cfg.swapColorBytes = boardCfg.display.swapColorBytes;
    cfg.invertColors = boardCfg.display.invertColors;
    cfg.spiHz = boardCfg.display.spiHz;
    cfg.minRenderGapMs = boardCfg.display.minRenderGapMs;
    return cfg;
}

void SupervisorHMIModule::copyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";
    snprintf(out, outLen, "%s", in);
}

uint32_t SupervisorHMIModule::buildRenderKey_() const
{
    uint32_t h = 2166136261U;
    auto mix = [&h](const void* ptr, size_t len) {
        if (!ptr || len == 0) return;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; i++) {
            h ^= (uint32_t)b[i];
            h *= 16777619U;
        }
    };

    mix(&view_.wifiConnected, sizeof(view_.wifiConnected));
    mix(&view_.wifiState, sizeof(view_.wifiState));
    mix(view_.ip, strnlen(view_.ip, sizeof(view_.ip)) + 1U);
    mix(&view_.flowLinkOk, sizeof(view_.flowLinkOk));
    mix(&view_.flowMqttReady, sizeof(view_.flowMqttReady));
    mix(&view_.flowHasPoolModes, sizeof(view_.flowHasPoolModes));
    mix(&view_.flowFiltrationAuto, sizeof(view_.flowFiltrationAuto));
    mix(&view_.flowWinterMode, sizeof(view_.flowWinterMode));
    mix(&view_.flowPhAutoMode, sizeof(view_.flowPhAutoMode));
    mix(&view_.flowOrpAutoMode, sizeof(view_.flowOrpAutoMode));
    mix(&view_.flowFiltrationOn, sizeof(view_.flowFiltrationOn));
    mix(&view_.flowPhPumpOn, sizeof(view_.flowPhPumpOn));
    mix(&view_.flowChlorinePumpOn, sizeof(view_.flowChlorinePumpOn));
    mix(&view_.flowHasPh, sizeof(view_.flowHasPh));
    mix(&view_.flowPhValue, sizeof(view_.flowPhValue));
    mix(&view_.flowHasOrp, sizeof(view_.flowHasOrp));
    mix(&view_.flowOrpValue, sizeof(view_.flowOrpValue));
    mix(&view_.flowHasWaterTemp, sizeof(view_.flowHasWaterTemp));
    mix(&view_.flowWaterTemp, sizeof(view_.flowWaterTemp));
    mix(&view_.flowHasAirTemp, sizeof(view_.flowHasAirTemp));
    mix(&view_.flowAirTemp, sizeof(view_.flowAirTemp));
    mix(&view_.flowAlarmActiveCount, sizeof(view_.flowAlarmActiveCount));
    mix(&view_.flowAlarmCodeCount, sizeof(view_.flowAlarmCodeCount));
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); i++) {
        mix(view_.flowAlarmCodes[i], strnlen(view_.flowAlarmCodes[i], sizeof(view_.flowAlarmCodes[i])) + 1U);
    }
    mix(&view_.flowAlarmActCount, sizeof(view_.flowAlarmActCount));
    mix(&view_.flowAlarmAckCount, sizeof(view_.flowAlarmAckCount));
    mix(&view_.flowAlarmClrCount, sizeof(view_.flowAlarmClrCount));
    mix(view_.flowAlarmStates, sizeof(view_.flowAlarmStates));
    mix(&view_.wifiResetPending, sizeof(view_.wifiResetPending));
    mix(view_.banner, strnlen(view_.banner, sizeof(view_.banner)) + 1U);

    return h;
}

uint32_t SupervisorHMIModule::currentClockMinute_() const
{
    const time_t now = time(nullptr);
    if (now <= 1600000000) return 0U;
    return (uint32_t)(now / 60);
}

uint32_t SupervisorHMIModule::currentPageCycle_() const
{
    return (uint32_t)(millis() / kPageRotateMs);
}

void SupervisorHMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    flowCfgSvc_ = services.get<FlowCfgRemoteService>(ServiceId::FlowCfg);
    (void)logHub_;

    if (pirPin_ >= 0) {
        int pirMode = INPUT;
#if defined(ESP32)
        // ESP32 GPIO 34..39 are input-only and don't support internal pull resistors.
        if (pirPin_ <= 33) {
            pirMode = pirActiveHigh_ ? INPUT_PULLDOWN : INPUT_PULLUP;
        }
#endif
        pinMode(pirPin_, pirMode);
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawPir = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        pirRawState_ = rawPir;
        pirStableState_ = rawPir;
        pirDebounceChangedAtMs_ = millis();
    }
    if (wifiResetPin_ >= 0) {
        pinMode(wifiResetPin_, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(wifiResetPin_) == LOW);
        buttonRawPressed_ = rawPressed;
        buttonStablePressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = millis();
    }

    lastMotionMs_ = millis();
    copyText_(view_.fwState, sizeof(view_.fwState), "idle");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), "none");
    setDefaultBanner_();

    LOGI("Supervisor HMI initialized tft_cs=%d tft_dc=%d tft_rst=%d tft_bl=%d tft_mosi=%d tft_sclk=%d rot=%d colstart=%d rowstart=%d pir=%d pir_active_high=%d wifi_reset=%d",
         (int)driverCfg_.csPin,
         (int)driverCfg_.dcPin,
         (int)driverCfg_.rstPin,
         (int)driverCfg_.backlightPin,
         (int)driverCfg_.mosiPin,
         (int)driverCfg_.sclkPin,
         (int)driverCfg_.rotation,
         (int)driverCfg_.colStart,
         (int)driverCfg_.rowStart,
         (int)pirPin_,
         (int)(pirActiveHigh_ ? 1 : 0),
         (int)wifiResetPin_);
}

void SupervisorHMIModule::pollWifiAndNetwork_()
{
    view_.wifiConnected = false;
    view_.wifiState = WifiState::Idle;
    view_.accessMode = NetworkAccessMode::None;
    view_.netReachable = false;
    view_.hasRssi = false;
    view_.rssiDbm = -127;
    view_.ip[0] = '\0';

    if (wifiSvc_) {
        if (wifiSvc_->state) view_.wifiState = wifiSvc_->state(wifiSvc_->ctx);
        if (wifiSvc_->isConnected) view_.wifiConnected = wifiSvc_->isConnected(wifiSvc_->ctx);
        if (wifiSvc_->getIP) (void)wifiSvc_->getIP(wifiSvc_->ctx, view_.ip, sizeof(view_.ip));
    }

    if (netAccessSvc_) {
        if (netAccessSvc_->mode) view_.accessMode = netAccessSvc_->mode(netAccessSvc_->ctx);
        if (netAccessSvc_->isWebReachable) view_.netReachable = netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
        if (view_.ip[0] == '\0' && netAccessSvc_->getIP) {
            (void)netAccessSvc_->getIP(netAccessSvc_->ctx, view_.ip, sizeof(view_.ip));
        }
    }

    if (view_.wifiConnected && WiFi.status() == WL_CONNECTED) {
        view_.rssiDbm = (int32_t)WiFi.RSSI();
        view_.hasRssi = true;
    }
}

void SupervisorHMIModule::pollFirmwareStatus_()
{
    if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) return;

    char json[320] = {0};
    if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, json, sizeof(json))) return;

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    copyText_(view_.fwState, sizeof(view_.fwState), root["state"] | "n/a");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), root["target"] | "n/a");
    view_.fwProgress = (uint8_t)(root["progress"] | 0U);
    copyText_(view_.fwMsg, sizeof(view_.fwMsg), root["msg"] | "");
    view_.fwBusy = root["busy"] | false;
    view_.fwPending = root["pending"] | false;
    fwBusyOrPending_ = view_.fwBusy || view_.fwPending;
}

void SupervisorHMIModule::pollFlowStatus_()
{
    if (!flowCfgSvc_ || !flowCfgSvc_->runtimeStatusDomainJson) return;
    if (flowCfgSvc_->isReady && !flowCfgSvc_->isReady(flowCfgSvc_->ctx)) return;

    SupervisorHmiViewModel nextView = view_;
    bool anyDomainOk = false;
    StaticJsonDocument<640> doc;

    auto fetchDomain = [&](FlowStatusDomain domain) -> JsonObjectConst {
        memset(flowStatusScratchBuf_, 0, sizeof(flowStatusScratchBuf_));
        if (!flowCfgSvc_->runtimeStatusDomainJson(flowCfgSvc_->ctx, domain, flowStatusScratchBuf_, sizeof(flowStatusScratchBuf_))) {
            doc.clear();
            return JsonObjectConst();
        }
        doc.clear();
        const DeserializationError err = deserializeJson(doc, flowStatusScratchBuf_);
        if (!err && doc.is<JsonObjectConst>()) {
            JsonObjectConst root = doc.as<JsonObjectConst>();
            if (root["ok"] | false) {
                anyDomainOk = true;
                return root;
            }
        }
        doc.clear();
        return JsonObjectConst();
    };

    {
        JsonObjectConst root = fetchDomain(FlowStatusDomain::System);
        if (!root.isNull()) {
            nextView.flowFirmware[0] = '\0';
            nextView.flowHasHeapFrag = false;
            nextView.flowHeapFragPct = 0;
            copyText_(nextView.flowFirmware, sizeof(nextView.flowFirmware), root["fw"] | "");
            JsonObjectConst flowHeap = root["heap"];
            if (!flowHeap.isNull()) {
                nextView.flowHasHeapFrag = true;
                nextView.flowHeapFragPct = (uint8_t)(flowHeap["frag"] | 0U);
            }
        }
    }

    {
        JsonObjectConst root = fetchDomain(FlowStatusDomain::Wifi);
        if (!root.isNull()) {
            nextView.flowHasRssi = false;
            nextView.flowRssiDbm = -127;
            JsonObjectConst flowWifi = root["wifi"];
            nextView.flowHasRssi = flowWifi["hrss"] | false;
            nextView.flowRssiDbm = (int32_t)(flowWifi["rssi"] | -127);
        }
    }

    {
        JsonObjectConst root = fetchDomain(FlowStatusDomain::Mqtt);
        if (!root.isNull()) {
            nextView.flowMqttReady = false;
            nextView.flowMqttRxDrop = 0;
            nextView.flowMqttParseFail = 0;
            JsonObjectConst flowMqtt = root["mqtt"];
            nextView.flowMqttReady = flowMqtt["rdy"] | false;
            nextView.flowMqttRxDrop = (uint32_t)(flowMqtt["rxdrp"] | 0U);
            nextView.flowMqttParseFail = (uint32_t)(flowMqtt["prsf"] | 0U);
        }
    }

    {
        JsonObjectConst root = fetchDomain(FlowStatusDomain::I2c);
        if (!root.isNull()) {
            nextView.flowLinkOk = false;
            nextView.flowI2cReqCount = 0;
            nextView.flowI2cBadReqCount = 0;
            nextView.flowI2cLastReqAgoMs = 0;
            JsonObjectConst i2c = root["i2c"];
            nextView.flowLinkOk = i2c["lnk"] | false;
            nextView.flowI2cReqCount = (uint32_t)(i2c["req"] | 0U);
            nextView.flowI2cBadReqCount = (uint32_t)(i2c["breq"] | 0U);
            nextView.flowI2cLastReqAgoMs = (uint32_t)(i2c["ago"] | 0U);
        }
    }

    {
        JsonObjectConst root = fetchDomain(FlowStatusDomain::Pool);
        if (!root.isNull()) {
            nextView.flowHasPoolModes = false;
            nextView.flowFiltrationAuto = false;
            nextView.flowWinterMode = false;
            nextView.flowPhAutoMode = false;
            nextView.flowOrpAutoMode = false;
            nextView.flowFiltrationOn = false;
            nextView.flowPhPumpOn = false;
            nextView.flowChlorinePumpOn = false;
            nextView.flowHasPh = false;
            nextView.flowPhValue = 0.0f;
            nextView.flowHasOrp = false;
            nextView.flowOrpValue = 0.0f;
            nextView.flowHasWaterTemp = false;
            nextView.flowWaterTemp = 0.0f;
            nextView.flowHasAirTemp = false;
            nextView.flowAirTemp = 0.0f;
            JsonObjectConst poolMode = root["pool"];
            if (!poolMode.isNull()) {
                nextView.flowHasPoolModes = poolMode["has"] | false;
                nextView.flowFiltrationAuto = poolMode["auto"] | false;
                nextView.flowWinterMode = poolMode["wint"] | false;
                nextView.flowPhAutoMode = poolMode["pha"] | false;
                nextView.flowOrpAutoMode = poolMode["ora"] | false;
                nextView.flowFiltrationOn = poolMode["fil"] | false;
                nextView.flowPhPumpOn = poolMode["php"] | false;
                nextView.flowChlorinePumpOn = poolMode["clp"] | false;
                JsonVariantConst phVar = poolMode["ph"];
                if (!phVar.isNull()) {
                    nextView.flowHasPh = true;
                    nextView.flowPhValue = phVar.as<float>();
                }
                JsonVariantConst orpVar = poolMode["orp"];
                if (!orpVar.isNull()) {
                    nextView.flowHasOrp = true;
                    nextView.flowOrpValue = orpVar.as<float>();
                }
                JsonVariantConst waterTempVar = poolMode["wat"];
                if (!waterTempVar.isNull()) {
                    nextView.flowHasWaterTemp = true;
                    nextView.flowWaterTemp = waterTempVar.as<float>();
                }
                JsonVariantConst airTempVar = poolMode["air"];
                if (!airTempVar.isNull()) {
                    nextView.flowHasAirTemp = true;
                    nextView.flowAirTemp = airTempVar.as<float>();
                }
            }
        }
    }

    nextView.flowAlarmActCount = 0;
    nextView.flowAlarmAckCount = 0;
    nextView.flowAlarmClrCount = 0;
    for (size_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        switch (nextView.flowAlarmStates[i]) {
            case SupervisorAlarmState::Active:
                ++nextView.flowAlarmActCount;
                break;
            case SupervisorAlarmState::Acked:
                ++nextView.flowAlarmAckCount;
                break;
            case SupervisorAlarmState::Clear:
            default:
                ++nextView.flowAlarmClrCount;
                break;
        }
    }

    if (!anyDomainOk) return;

    nextView.flowCfgReady = true;
    view_.flowCfgReady = nextView.flowCfgReady;
    view_.flowLinkOk = nextView.flowLinkOk;
    copyText_(view_.flowFirmware, sizeof(view_.flowFirmware), nextView.flowFirmware);
    view_.flowHasRssi = nextView.flowHasRssi;
    view_.flowRssiDbm = nextView.flowRssiDbm;
    view_.flowHasHeapFrag = nextView.flowHasHeapFrag;
    view_.flowHeapFragPct = nextView.flowHeapFragPct;
    view_.flowMqttReady = nextView.flowMqttReady;
    view_.flowMqttRxDrop = nextView.flowMqttRxDrop;
    view_.flowMqttParseFail = nextView.flowMqttParseFail;
    view_.flowI2cReqCount = nextView.flowI2cReqCount;
    view_.flowI2cBadReqCount = nextView.flowI2cBadReqCount;
    view_.flowI2cLastReqAgoMs = nextView.flowI2cLastReqAgoMs;
    view_.flowHasPoolModes = nextView.flowHasPoolModes;
    view_.flowFiltrationAuto = nextView.flowFiltrationAuto;
    view_.flowWinterMode = nextView.flowWinterMode;
    view_.flowPhAutoMode = nextView.flowPhAutoMode;
    view_.flowOrpAutoMode = nextView.flowOrpAutoMode;
    view_.flowFiltrationOn = nextView.flowFiltrationOn;
    view_.flowPhPumpOn = nextView.flowPhPumpOn;
    view_.flowChlorinePumpOn = nextView.flowChlorinePumpOn;
    view_.flowHasPh = nextView.flowHasPh;
    view_.flowPhValue = nextView.flowPhValue;
    view_.flowHasOrp = nextView.flowHasOrp;
    view_.flowOrpValue = nextView.flowOrpValue;
    view_.flowHasWaterTemp = nextView.flowHasWaterTemp;
    view_.flowWaterTemp = nextView.flowWaterTemp;
    view_.flowHasAirTemp = nextView.flowHasAirTemp;
    view_.flowAirTemp = nextView.flowAirTemp;
    view_.flowAlarmActiveCount = nextView.flowAlarmActiveCount;
    view_.flowAlarmCodeCount = nextView.flowAlarmCodeCount;
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); ++i) {
        copyText_(view_.flowAlarmCodes[i], sizeof(view_.flowAlarmCodes[i]), nextView.flowAlarmCodes[i]);
    }
    view_.flowAlarmActCount = nextView.flowAlarmActCount;
    view_.flowAlarmAckCount = nextView.flowAlarmAckCount;
    view_.flowAlarmClrCount = nextView.flowAlarmClrCount;
    for (size_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        view_.flowAlarmStates[i] = nextView.flowAlarmStates[i];
    }
}

void SupervisorHMIModule::triggerWifiReset_()
{
    if (wifiResetPending_) return;
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: config unavailable");
        return;
    }

    static constexpr const char* kWifiResetPatch = "{\"wifi\":{\"enabled\":true,\"ssid\":\"\",\"pass\":\"\"}}";
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, kWifiResetPatch);
    if (!ok) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: applyJson");
        return;
    }

    if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
        (void)netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }

    wifiResetPending_ = true;
    restartAtMs_ = millis() + 1200U;
    copyText_(view_.banner, sizeof(view_.banner), "WiFi credentials reset: rebooting...");
    LOGW("WiFi credentials reset requested from local button");
}

void SupervisorHMIModule::updateWifiResetButton_()
{
    if (wifiResetPin_ < 0) return;

    const uint32_t now = millis();
    const bool rawPressed = (digitalRead(wifiResetPin_) == LOW);

    if (rawPressed != buttonRawPressed_) {
        buttonRawPressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = now;
    }
    if ((uint32_t)(now - buttonDebounceChangedAtMs_) >= wifiResetDebounceMs_) {
        buttonStablePressed_ = buttonRawPressed_;
    }

    const bool isPressed = buttonStablePressed_;

    // Safety: do not accept press events until we've seen a stable released
    // level after boot. This avoids false long-press triggers on floating lines.
    if (!buttonArmed_) {
        if (isPressed) {
            buttonHighSinceMs_ = 0;
            buttonPressed_ = false;
            buttonTriggered_ = false;
            return;
        }

        if (buttonHighSinceMs_ == 0U) {
            buttonHighSinceMs_ = now;
            return;
        }

        if ((uint32_t)(now - buttonHighSinceMs_) < kButtonArmHighStableMs) {
            return;
        }
        if (now < kButtonBootGuardMs) {
            return;
        }

        buttonArmed_ = true;
        LOGI("WiFi reset button armed");
        return;
    }

    if (!isPressed) {
        buttonPressed_ = false;
        buttonTriggered_ = false;
        return;
    }

    if (!buttonPressed_) {
        buttonPressed_ = true;
        buttonPressedAtMs_ = now;
        return;
    }

    if (buttonTriggered_) return;
    if ((uint32_t)(now - buttonPressedAtMs_) < wifiResetHoldMs_) return;

    buttonTriggered_ = true;
    triggerWifiReset_();
}

void SupervisorHMIModule::updateBacklight_()
{
    const uint32_t now = millis();
    const bool forceBacklightOn = (int32_t)(now - backlightForceOnUntilMs_) < 0;
    bool motion = false;
    if (pirPin_ >= 0) {
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawMotion = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        if (rawMotion != pirRawState_) {
            pirRawState_ = rawMotion;
            pirDebounceChangedAtMs_ = now;
        }
        if ((uint32_t)(now - pirDebounceChangedAtMs_) >= pirDebounceMs_) {
            pirStableState_ = pirRawState_;
        }
        motion = pirStableState_;
    }
    if (motion || fwBusyOrPending_) {
        lastMotionMs_ = now;
    }

    if (pirPin_ < 0) {
        driver_.setBacklight(true);
        return;
    }

    const bool keepOn = forceBacklightOn || fwBusyOrPending_ || ((uint32_t)(now - lastMotionMs_) <= pirTimeoutMs_);
    driver_.setBacklight(keepOn);
}

void SupervisorHMIModule::rebuildBanner_()
{
    if (wifiResetPending_) return;
    if (!driver_.isBacklightOn()) {
        copyText_(view_.banner, sizeof(view_.banner), "PIR idle: backlight off");
        return;
    }
    setDefaultBanner_();
}

void SupervisorHMIModule::setDefaultBanner_()
{
    if (wifiResetPin_ < 0) {
        copyText_(view_.banner, sizeof(view_.banner), "Local display ready");
        return;
    }
    const uint32_t holdSeconds = (wifiResetHoldMs_ + 999U) / 1000U;
    snprintf(view_.banner, sizeof(view_.banner), "Hold WiFi button %lus to reset credentials", (unsigned long)holdSeconds);
}

void SupervisorHMIModule::loop()
{
    if (!driverReady_) {
        driverReady_ = driver_.begin();
        if (!driverReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
        lastRenderMs_ = 0;
        splashHoldUntilMs_ = millis() + kStartupSplashHoldMs;
        backlightForceOnUntilMs_ = millis() + kStartupBacklightForceOnMs;
        hasLastRenderKey_ = false;
        lastRenderedMinute_ = 0;
        lastRenderedPageCycle_ = 0;
        lastBacklightOn_ = driver_.isBacklightOn();
    }

    pollWifiAndNetwork_();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastFwPollMs_) >= kFwPollMs) {
        lastFwPollMs_ = now;
        pollFirmwareStatus_();
        pollFlowStatus_();
    }

    updateWifiResetButton_();
    updateBacklight_();
    rebuildBanner_();

    view_.wifiResetPending = wifiResetPending_;

    if ((int32_t)(millis() - splashHoldUntilMs_) < 0) {
        vTaskDelay(pdMS_TO_TICKS(25));
        return;
    }

    const bool backlightOn = driver_.isBacklightOn();
    const uint32_t renderKey = buildRenderKey_();
    const uint32_t minuteKey = currentClockMinute_();
    const uint32_t pageCycleKey = currentPageCycle_();
    const bool changed = (!hasLastRenderKey_) ||
                         (renderKey != lastRenderKey_) ||
                         (minuteKey != lastRenderedMinute_) ||
                         (pageCycleKey != lastRenderedPageCycle_) ||
                         (backlightOn != lastBacklightOn_);
    if (changed && backlightOn) {
        (void)driver_.render(view_, !hasLastRenderKey_);
        lastRenderMs_ = now;
        lastRenderKey_ = renderKey;
        lastRenderedMinute_ = minuteKey;
        lastRenderedPageCycle_ = pageCycleKey;
        hasLastRenderKey_ = true;
    }
    lastBacklightOn_ = backlightOn;

    if (restartAtMs_ != 0U && (int32_t)(millis() - restartAtMs_) >= 0) {
        delay(30);
        esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(25));
}
