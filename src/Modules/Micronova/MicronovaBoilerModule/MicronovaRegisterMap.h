#pragma once

#include "Modules/Micronova/MicronovaBusModule/MicronovaProtocol.h"

#include <stdint.h>

struct MicronovaRegisterDef {
    const char* key;
    uint8_t readCode;
    uint8_t writeCode;
    uint8_t address;
    float scale;
    float offset;
    bool writable;
    bool enabled;
    uint32_t pollIntervalMs;
};

struct MicronovaCommandDef {
    const char* key;
    uint8_t writeCode;
    uint8_t address;
    uint8_t value;
    uint8_t repeatCount;
    uint16_t repeatDelayMs;
};

enum class MicronovaRegisterId : uint8_t {
    StoveState = 0,
    RoomTemperature,
    FumesTemperature,
    PowerLevel,
    FanSpeed,
    TargetTemperature,
    WaterTemperature,
    WaterPressure,
    AlarmCode,
    Count
};

constexpr uint8_t kMicronovaRegisterCount = (uint8_t)MicronovaRegisterId::Count;

static constexpr MicronovaRegisterDef kMicronovaDefaultRegisters[kMicronovaRegisterCount] = {
    {"stove_state", MicronovaProtocol::RamRead, 0x00, 0x21, 1.0f, 0.0f, false, true, 1800000UL},
    {"room_temperature", MicronovaProtocol::RamRead, 0x00, 0x01, 0.5f, 0.0f, false, true, 1800000UL},
    {"fumes_temperature", MicronovaProtocol::RamRead, 0x00, 0x3E, 1.0f, 0.0f, false, true, 1800000UL},
    {"power_level", MicronovaProtocol::EepromRead, MicronovaProtocol::EepromWrite, 0x89, 1.0f, 0.0f, true, true, 1800000UL},
    {"fan_speed", MicronovaProtocol::EepromRead, MicronovaProtocol::EepromWrite, 0x8A, 1.0f, 0.0f, true, true, 1800000UL},
    {"target_temperature", MicronovaProtocol::EepromRead, MicronovaProtocol::EepromWrite, 0x8B, 1.0f, 0.0f, true, true, 1800000UL},
    {"water_temperature", 0x00, 0x00, 0x00, 1.0f, 0.0f, false, false, 1800000UL},
    {"water_pressure", 0x00, 0x00, 0x00, 1.0f, 0.0f, false, false, 1800000UL},
    {"alarm_code", 0x00, 0x00, 0x00, 1.0f, 0.0f, false, false, 1800000UL},
};

static constexpr MicronovaCommandDef kMicronovaPowerOnDefault{
    "power_on",
    MicronovaProtocol::RamWrite,
    0x58,
    0x5A,
    10,
    MicronovaProtocol::DefaultRepeatDelayMs
};

static constexpr MicronovaCommandDef kMicronovaPowerOffDefault{
    "power_off",
    MicronovaProtocol::RamWrite,
    0x21,
    0x00,
    10,
    MicronovaProtocol::DefaultRepeatDelayMs
};
