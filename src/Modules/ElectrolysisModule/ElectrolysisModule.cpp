/**
 * @file ElectrolysisModule.cpp
 * @brief I2C client for a dedicated electrolysis controller.
 */

#include "ElectrolysisModule.h"

#include <Arduino.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::ElectrolysisModule)
#include "Core/ModuleLog.h"

namespace {
constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Electrolysis;
constexpr uint8_t kCfgBranch = 1;
constexpr uint16_t kMinPollPeriodMs = 100;
constexpr uint16_t kDefaultPollPeriodMs = 500;
constexpr uint16_t kMaxPollPeriodMs = 5000;

uint16_t boundedPollPeriod_(uint16_t value)
{
    if (value < kMinPollPeriodMs) return kDefaultPollPeriodMs;
    if (value > kMaxPollPeriodMs) return kMaxPollPeriodMs;
    return value;
}

uint8_t normalizedAddress_(uint8_t address)
{
    if (address < 0x08U || address > 0x77U) return ElectrolysisProtocol::PreferredAddress;
    return address;
}
}  // namespace

void ElectrolysisModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    if (!services.add(ServiceId::Electrolysis, &service_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Electrolysis));
    }

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranch);
    cfg.registerVar(addressVar_, kCfgModuleId, kCfgBranch);
    cfg.registerVar(pollPeriodVar_, kCfgModuleId, kCfgBranch);
    cfg.registerVar(maxCurrentVar_, kCfgModuleId, kCfgBranch);

    request_.enable = 0;
    request_.productionPct = 0;
    request_.maxCurrentMa = cfgData_.maxCurrentMa;
    LOGI("Electrolysis client registered");
}

void ElectrolysisModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    cfgData_.address = normalizedAddress_(cfgData_.address);
    cfgData_.pollPeriodMs = boundedPollPeriod_(cfgData_.pollPeriodMs);

    portENTER_CRITICAL(&requestMux_);
    request_.maxCurrentMa = cfgData_.maxCurrentMa;
    portEXIT_CRITICAL(&requestMux_);

    LOGI("Electrolysis client %s addr=0x%02X poll=%ums max_current=%umA",
         cfgData_.enabled ? "enabled" : "disabled",
         (unsigned)cfgData_.address,
         (unsigned)cfgData_.pollPeriodMs,
         (unsigned)cfgData_.maxCurrentMa);
}

void ElectrolysisModule::loop()
{
    const uint32_t nowMs = millis();
    if (!cfgData_.enabled) {
        setOnline_(false, nowMs);
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    const uint16_t periodMs = boundedPollPeriod_(cfgData_.pollPeriodMs);
    if ((uint32_t)(nowMs - lastPollMs_) >= periodMs) {
        lastPollMs_ = nowMs;
        pollController_(nowMs);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}

uint8_t ElectrolysisModule::availableStatic_(void* ctx)
{
    ElectrolysisModule* self = static_cast<ElectrolysisModule*>(ctx);
    return self ? self->available_() : 0U;
}

ElectrolysisSvcStatus ElectrolysisModule::writeRequestStatic_(void* ctx, const ElectrolysisRequest* request)
{
    ElectrolysisModule* self = static_cast<ElectrolysisModule*>(ctx);
    return self ? self->writeRequest_(request) : ELECTROLYSIS_SVC_ERR_INVALID_ARG;
}

ElectrolysisSvcStatus ElectrolysisModule::readRuntimeStatic_(void* ctx, ElectrolysisRuntime* outRuntime)
{
    ElectrolysisModule* self = static_cast<ElectrolysisModule*>(ctx);
    return self ? self->readRuntime_(outRuntime) : ELECTROLYSIS_SVC_ERR_INVALID_ARG;
}

uint8_t ElectrolysisModule::available_() const
{
    return cfgData_.enabled ? 1U : 0U;
}

ElectrolysisSvcStatus ElectrolysisModule::writeRequest_(const ElectrolysisRequest* request)
{
    if (!request) return ELECTROLYSIS_SVC_ERR_INVALID_ARG;
    if (!cfgData_.enabled) return ELECTROLYSIS_SVC_ERR_DISABLED;

    ElectrolysisRequest copy = *request;
    copy.productionPct = ElectrolysisProtocol::clampProductionPct(copy.productionPct);
    copy.deadtimeMs = ElectrolysisProtocol::clampDeadtimeMs(copy.deadtimeMs);
    copy.productionWindowS = ElectrolysisProtocol::clampProductionWindowS(copy.productionWindowS);
    copy.maxCurrentMa = cfgData_.maxCurrentMa ? cfgData_.maxCurrentMa : copy.maxCurrentMa;

    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&requestMux_);
    request_ = copy;
    portEXIT_CRITICAL(&requestMux_);

    portENTER_CRITICAL(&runtimeMux_);
    runtime_.lastCommandMs = nowMs;
    portEXIT_CRITICAL(&runtimeMux_);
    return ELECTROLYSIS_SVC_OK;
}

ElectrolysisSvcStatus ElectrolysisModule::readRuntime_(ElectrolysisRuntime* outRuntime)
{
    if (!outRuntime) return ELECTROLYSIS_SVC_ERR_INVALID_ARG;
    portENTER_CRITICAL(&runtimeMux_);
    *outRuntime = runtime_;
    portEXIT_CRITICAL(&runtimeMux_);
    return ELECTROLYSIS_SVC_OK;
}

void ElectrolysisModule::pollController_(uint32_t nowMs)
{
    if (!ioSvc_ || !ioSvc_->i2cTransfer) {
        setOnline_(false, nowMs);
        return;
    }

    ElectrolysisRequest request{};
    portENTER_CRITICAL(&requestMux_);
    request = request_;
    portEXIT_CRITICAL(&requestMux_);

    ElectrolysisProtocol::CommandFrame command = buildCommandFrame_(request);
    ElectrolysisProtocol::StatusFrame status{};

    const IoStatus st = ioSvc_->i2cTransfer(ioSvc_->ctx,
                                            cfgData_.address,
                                            reinterpret_cast<const uint8_t*>(&command),
                                            ElectrolysisProtocol::CommandFrameSize,
                                            reinterpret_cast<uint8_t*>(&status),
                                            ElectrolysisProtocol::StatusFrameSize,
                                            50U);
    if (st != IO_OK) {
        setOnline_(false, nowMs);
        warnTransferFailure_(st, nowMs);
        return;
    }

    if (status.version != ElectrolysisProtocol::Version ||
        !ElectrolysisProtocol::statusCrcOk(status) ||
        !ElectrolysisProtocol::validState(status.state)) {
        setOnline_(false, nowMs);
        if ((uint32_t)(nowMs - lastWarnMs_) >= 10000U) {
            LOGW("bad electrolysis status version=%u state=%u crc_ok=%u",
                 (unsigned)status.version,
                 (unsigned)status.state,
                 ElectrolysisProtocol::statusCrcOk(status) ? 1u : 0u);
            lastWarnMs_ = nowMs;
        }
        return;
    }

    updateRuntimeFromStatus_(status, nowMs);
}

void ElectrolysisModule::setOnline_(bool online, uint32_t nowMs)
{
    portENTER_CRITICAL(&runtimeMux_);
    runtime_.online = online ? 1U : 0U;
    if (!online) {
        runtime_.state = ElectrolysisProtocol::StateIdle;
        runtime_.productionAppliedPct = 0;
        runtime_.polarity = ElectrolysisProtocol::PolarityOff;
    }
    runtime_.lastSeenMs = online ? nowMs : runtime_.lastSeenMs;
    portEXIT_CRITICAL(&runtimeMux_);
}

void ElectrolysisModule::updateRuntimeFromStatus_(const ElectrolysisProtocol::StatusFrame& status, uint32_t nowMs)
{
    portENTER_CRITICAL(&runtimeMux_);
    runtime_.online = 1U;
    runtime_.state = status.state;
    runtime_.faultMask = status.faultMask;
    runtime_.flowOk = status.flowOk;
    runtime_.tempC10 = status.tempC10;
    runtime_.currentMa = status.currentMa;
    runtime_.voltageMv = status.voltageMv;
    runtime_.polarity = status.polarity;
    runtime_.productionAppliedPct = status.productionAppliedPct;
    runtime_.lastReverseS = status.lastReverseS;
    runtime_.uptimeS = status.uptimeS;
    runtime_.lastSeenMs = nowMs;
    portEXIT_CRITICAL(&runtimeMux_);
}

ElectrolysisProtocol::CommandFrame ElectrolysisModule::buildCommandFrame_(const ElectrolysisRequest& request)
{
    ElectrolysisProtocol::CommandFrame frame{};
    frame.seq = ++seq_;
    frame.controlFlags = request.resetFaults ? ElectrolysisProtocol::CommandFlagResetFaults
                                             : ElectrolysisProtocol::CommandFlagNone;
    frame.enable = request.enable ? 1U : 0U;
    frame.productionPct = request.enable ? request.productionPct : 0U;
    frame.startDelayS = request.startDelayS;
    frame.productionWindowS = request.productionWindowS;
    frame.reversePeriodMin = request.reversePeriodMin;
    frame.deadtimeMs = request.deadtimeMs;
    frame.minWaterTempC10 = request.minWaterTempC10;
    frame.maxCurrentMa = cfgData_.maxCurrentMa ? cfgData_.maxCurrentMa : request.maxCurrentMa;
    frame.heartbeat = ++heartbeat_;
    ElectrolysisProtocol::sealCommand(frame);
    return frame;
}

void ElectrolysisModule::warnTransferFailure_(IoStatus st, uint32_t nowMs)
{
    if ((uint32_t)(nowMs - lastWarnMs_) < 10000U) return;
    LOGW("electrolysis i2c transfer failed addr=0x%02X st=%u",
         (unsigned)cfgData_.address,
         (unsigned)st);
    lastWarnMs_ = nowMs;
}
