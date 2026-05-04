#pragma once
/**
 * @file ElectrolysisProtocol.h
 * @brief Shared I2C command/status contract for a dedicated electrolysis controller.
 */

#include <stddef.h>
#include <stdint.h>

namespace ElectrolysisProtocol {

constexpr uint8_t Version = 1;
constexpr uint8_t PreferredAddress = 0x43;
constexpr uint8_t AlternateAddress = 0x45;

constexpr uint8_t MaxProductionPct = 100;
constexpr uint16_t MinDeadtimeMs = 200;
constexpr uint16_t DefaultDeadtimeMs = 5000;
constexpr uint16_t MaxDeadtimeMs = 10000;
constexpr uint16_t DefaultStartDelayS = 30;
constexpr uint16_t DefaultProductionWindowS = 300;
constexpr uint16_t MaxProductionWindowS = 3600;
constexpr int16_t DefaultMinWaterTempC10 = 150;
constexpr uint16_t DefaultReversePeriodMin = 180;
constexpr uint32_t DefaultHeartbeatTimeoutMs = 3000;

enum State : uint8_t {
    StateIdle = 0,
    StateWaitFlow = 1,
    StateWaitTemp = 2,
    StateStarting = 3,
    StateRunningForward = 4,
    StateRunningReverse = 5,
    StateDeadtimeBeforeReverse = 6,
    StateStopping = 7,
    StateFault = 8
};

enum Polarity : int8_t {
    PolarityReverse = -1,
    PolarityOff = 0,
    PolarityForward = 1
};

enum CommandFlag : uint8_t {
    CommandFlagNone = 0,
    CommandFlagResetFaults = (1u << 0)
};

enum FaultBit : uint16_t {
    FaultNone = 0,
    FaultNoFlow = (1u << 0),
    FaultTemperature = (1u << 1),
    FaultOverCurrent = (1u << 2),
    FaultComTimeout = (1u << 3),
    FaultBadFrame = (1u << 4),
    FaultDriver = (1u << 5),
    FaultSensor = (1u << 6),
    FaultConfig = (1u << 7)
};

#pragma pack(push, 1)
struct CommandFrame {
    uint8_t version = Version;
    uint8_t seq = 0;
    uint8_t controlFlags = CommandFlagNone;
    uint8_t enable = 0;
    uint8_t productionPct = 0;
    uint16_t startDelayS = DefaultStartDelayS;
    uint16_t productionWindowS = DefaultProductionWindowS;
    uint16_t reversePeriodMin = DefaultReversePeriodMin;
    uint16_t deadtimeMs = DefaultDeadtimeMs;
    int16_t minWaterTempC10 = DefaultMinWaterTempC10;
    uint16_t maxCurrentMa = 0;
    uint8_t heartbeat = 0;
    uint8_t crc8 = 0;
};

struct StatusFrame {
    uint8_t version = Version;
    uint8_t seqAck = 0;
    uint8_t state = StateIdle;
    uint16_t faultMask = FaultNone;
    uint8_t flowOk = 0;
    int16_t tempC10 = 0;
    uint16_t currentMa = 0;
    uint16_t voltageMv = 0;
    int8_t polarity = PolarityOff;
    uint8_t productionAppliedPct = 0;
    uint32_t lastReverseS = 0;
    uint32_t uptimeS = 0;
    uint8_t crc8 = 0;
};
#pragma pack(pop)

constexpr size_t CommandFrameSize = sizeof(CommandFrame);
constexpr size_t StatusFrameSize = sizeof(StatusFrame);
constexpr size_t CommandCrcOffset = CommandFrameSize - 1;
constexpr size_t StatusCrcOffset = StatusFrameSize - 1;

static_assert(CommandFrameSize == 19, "Electrolysis command frame size changed");
static_assert(StatusFrameSize == 23, "Electrolysis status frame size changed");

inline uint8_t clampProductionPct(uint8_t value)
{
    return (value > MaxProductionPct) ? MaxProductionPct : value;
}

inline uint16_t clampDeadtimeMs(uint16_t value)
{
    if (value < MinDeadtimeMs) {
        return MinDeadtimeMs;
    }
    return (value > MaxDeadtimeMs) ? MaxDeadtimeMs : value;
}

inline uint16_t clampProductionWindowS(uint16_t value)
{
    if (value == 0) {
        return 1;
    }
    return (value > MaxProductionWindowS) ? MaxProductionWindowS : value;
}

inline bool validState(uint8_t state)
{
    return state <= StateFault;
}

inline bool isRunningState(uint8_t state)
{
    return state == StateRunningForward || state == StateRunningReverse;
}

inline bool faultActive(uint16_t faultMask, FaultBit bit)
{
    return (faultMask & (uint16_t)bit) != 0U;
}

inline uint8_t crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80U) ? (uint8_t)((crc << 1) ^ 0x07U) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

inline uint8_t commandCrc(const CommandFrame& frame)
{
    return crc8(reinterpret_cast<const uint8_t*>(&frame), CommandCrcOffset);
}

inline uint8_t statusCrc(const StatusFrame& frame)
{
    return crc8(reinterpret_cast<const uint8_t*>(&frame), StatusCrcOffset);
}

inline bool commandCrcOk(const CommandFrame& frame)
{
    return commandCrc(frame) == frame.crc8;
}

inline bool statusCrcOk(const StatusFrame& frame)
{
    return statusCrc(frame) == frame.crc8;
}

inline void sealCommand(CommandFrame& frame)
{
    frame.productionPct = clampProductionPct(frame.productionPct);
    frame.deadtimeMs = clampDeadtimeMs(frame.deadtimeMs);
    frame.productionWindowS = clampProductionWindowS(frame.productionWindowS);
    frame.crc8 = commandCrc(frame);
}

inline void sealStatus(StatusFrame& frame)
{
    frame.productionAppliedPct = clampProductionPct(frame.productionAppliedPct);
    frame.crc8 = statusCrc(frame);
}

}  // namespace ElectrolysisProtocol
