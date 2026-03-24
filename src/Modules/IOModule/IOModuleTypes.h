#pragma once
/**
 * @file IOModuleTypes.h
 * @brief Public POD types used to describe IO topology without runtime allocation.
 */

#include <stdint.h>

#include "Core/Services/IIO.h"
#include "Core/WokwiDefaultOverrides.h"

struct IOModuleConfig {
    bool enabled = FLOW_WIRDEF_IO_EN;
    int32_t i2cSda = FLOW_WIRDEF_IO_SDA;
    int32_t i2cScl = FLOW_WIRDEF_IO_SCL;
    int32_t adsPollMs = FLOW_MODDEF_IO_ADS;
    int32_t dsPollMs = FLOW_MODDEF_IO_DS;
    int32_t digitalPollMs = FLOW_MODDEF_IO_DIN;
    uint8_t adsInternalAddr = FLOW_WIRDEF_IO_AIAD;
    uint8_t adsExternalAddr = FLOW_WIRDEF_IO_AEAD;
    int32_t adsGain = FLOW_MODDEF_IO_AGAI;
    int32_t adsRate = FLOW_MODDEF_IO_ARAT;
    bool pcfEnabled = FLOW_WIRDEF_IO_PCFEN;
    uint8_t pcfAddress = FLOW_WIRDEF_IO_PCFAD;
    uint8_t pcfMaskDefault = FLOW_WIRDEF_IO_PCFMK;
    bool pcfActiveLow = FLOW_WIRDEF_IO_PCFAL;
    bool traceEnabled = FLOW_MODDEF_IO_TREN;
    int32_t tracePeriodMs = FLOW_MODDEF_IO_TRMS;
};

enum IOAnalogSource : uint8_t {
    IO_SRC_ADS_INTERNAL_SINGLE = 0,
    IO_SRC_ADS_EXTERNAL_DIFF = 1,
    IO_SRC_DS18_WATER = 2,
    IO_SRC_DS18_AIR = 3
};

typedef void (*IOAnalogValueCallback)(void* ctx, float value);
typedef void (*IODigitalValueCallback)(void* ctx, bool value);
typedef void (*IODigitalCounterValueCallback)(void* ctx, int32_t value);

enum IODigitalPullMode : uint8_t {
    IO_PULL_NONE = 0,
    IO_PULL_UP = 1,
    IO_PULL_DOWN = 2
};

enum IODigitalInputMode : uint8_t {
    IO_DIGITAL_INPUT_STATE = 0,
    IO_DIGITAL_INPUT_COUNTER = 1
};

struct IOAnalogDefinition {
    char id[24] = {0};
    /** Required explicit AI id in [IO_ID_AI_BASE..IO_ID_AI_BASE+MAX_ANALOG_ENDPOINTS). */
    IoId ioId = IO_ID_INVALID;
    uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
    uint8_t channel = 0;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    float minValid = -32768.0f;
    float maxValid = 32767.0f;
    IOAnalogValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
};

struct IOAnalogSlotConfig {
    char name[24] = {0};
    uint8_t source = IO_SRC_ADS_INTERNAL_SINGLE;
    uint8_t channel = 0;
    float c0 = 1.0f;
    float c1 = 0.0f;
    int32_t precision = 1;
    float minValid = -32768.0f;
    float maxValid = 32767.0f;
};

struct IODigitalOutputDefinition {
    char id[24] = {0};
    /** Required explicit DO id in [IO_ID_DO_BASE..IO_ID_DO_BASE+MAX_DIGITAL_OUTPUTS). */
    IoId ioId = IO_ID_INVALID;
    uint8_t pin = 0;
    bool activeHigh = false;
    bool initialOn = false;
    bool momentary = false;
    uint16_t pulseMs = 500;
};

struct IODigitalOutputSlotConfig {
    char name[24] = {0};
    uint8_t pin = 0;
    bool activeHigh = false;
    bool initialOn = false;
    bool momentary = false;
    int32_t pulseMs = 500;
};

struct IODigitalInputSlotConfig {
    char name[24] = {0};
    uint8_t pin = 0;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint32_t counterDebounceUs = 0;
};

struct IODigitalInputDefinition {
    char id[24] = {0};
    /** Required explicit DI id in [IO_ID_DI_BASE..IO_ID_DI_BASE+MAX_DIGITAL_INPUTS). */
    IoId ioId = IO_ID_INVALID;
    uint8_t pin = 0;
    bool activeHigh = true;
    uint8_t pullMode = IO_PULL_NONE;
    uint8_t mode = IO_DIGITAL_INPUT_STATE;
    uint32_t counterDebounceUs = 0;
    IODigitalValueCallback onValueChanged = nullptr;
    void* onValueCtx = nullptr;
    IODigitalCounterValueCallback onCounterChanged = nullptr;
    void* onCounterCtx = nullptr;
};
