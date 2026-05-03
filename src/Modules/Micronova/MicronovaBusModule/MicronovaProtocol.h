#pragma once

#include <stdint.h>

namespace MicronovaProtocol {

constexpr uint8_t RamRead = 0x00;
constexpr uint8_t RamWrite = 0x80;
constexpr uint8_t EepromRead = 0x20;
constexpr uint8_t EepromWrite = 0xA0;

constexpr uint32_t DefaultBaudrate = 1200U;
constexpr uint16_t DefaultReplyTimeoutMs = 200U;
constexpr uint16_t DefaultTurnaroundDelayMs = 80U;
constexpr uint16_t DefaultRepeatDelayMs = 100U;

static inline uint8_t writeChecksum(uint8_t writeCode, uint8_t address, uint8_t value)
{
    return (uint8_t)(writeCode + address + value);
}

static inline bool responseMatches(uint8_t readCode, uint8_t address, uint8_t checksum, uint8_t value)
{
    const uint8_t param = (uint8_t)(checksum - value);
    return param == address || param == (uint8_t)(readCode + address);
}

}  // namespace MicronovaProtocol
