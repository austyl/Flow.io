#pragma once
/**
 * @file IElectrolysis.h
 * @brief Dedicated electrolysis controller service interface.
 */

#include <stdint.h>

#include "Core/ElectrolysisProtocol.h"

enum ElectrolysisSvcStatus : uint8_t {
    ELECTROLYSIS_SVC_OK = 0,
    ELECTROLYSIS_SVC_ERR_INVALID_ARG = 1,
    ELECTROLYSIS_SVC_ERR_NOT_READY = 2,
    ELECTROLYSIS_SVC_ERR_DISABLED = 3,
    ELECTROLYSIS_SVC_ERR_COMM = 4,
    ELECTROLYSIS_SVC_ERR_BAD_STATUS = 5
};

struct ElectrolysisRequest {
    uint8_t enable = 0;
    uint8_t productionPct = 0;
    uint16_t startDelayS = ElectrolysisProtocol::DefaultStartDelayS;
    uint16_t productionWindowS = ElectrolysisProtocol::DefaultProductionWindowS;
    uint16_t reversePeriodMin = ElectrolysisProtocol::DefaultReversePeriodMin;
    uint16_t deadtimeMs = ElectrolysisProtocol::DefaultDeadtimeMs;
    int16_t minWaterTempC10 = ElectrolysisProtocol::DefaultMinWaterTempC10;
    uint16_t maxCurrentMa = 0;
    uint8_t resetFaults = 0;
};

struct ElectrolysisRuntime {
    uint8_t online = 0;
    uint8_t state = ElectrolysisProtocol::StateIdle;
    uint16_t faultMask = ElectrolysisProtocol::FaultNone;
    uint8_t flowOk = 0;
    int16_t tempC10 = 0;
    uint16_t currentMa = 0;
    uint16_t voltageMv = 0;
    int8_t polarity = ElectrolysisProtocol::PolarityOff;
    uint8_t productionAppliedPct = 0;
    uint32_t lastReverseS = 0;
    uint32_t uptimeS = 0;
    uint32_t lastSeenMs = 0;
    uint32_t lastCommandMs = 0;
};

struct ElectrolysisService {
    uint8_t (*available)(void* ctx);
    ElectrolysisSvcStatus (*writeRequest)(void* ctx, const ElectrolysisRequest* request);
    ElectrolysisSvcStatus (*readRuntime)(void* ctx, ElectrolysisRuntime* outRuntime);
    void* ctx;
};
