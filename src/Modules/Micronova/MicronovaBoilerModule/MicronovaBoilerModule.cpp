#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModule.h"

#include "Core/DataKeys.h"
#include "Core/Services/Services.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerTypes.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaBoilerModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr const char* kRegModuleNames[kMicronovaRegisterCount] = {
    "micronova/registers/stove_state",
    "micronova/registers/room_temperature",
    "micronova/registers/fumes_temperature",
    "micronova/registers/power_level",
    "micronova/registers/fan_speed",
    "micronova/registers/target_temperature",
    "micronova/registers/water_temperature",
    "micronova/registers/water_pressure",
    "micronova/registers/alarm_code",
};

static constexpr const char* kReadNvs[kMicronovaRegisterCount] = {
    "mnr0r", "mnr1r", "mnr2r", "mnr3r", "mnr4r", "mnr5r", "mnr6r", "mnr7r", "mnr8r"
};
static constexpr const char* kWriteNvs[kMicronovaRegisterCount] = {
    "mnr0w", "mnr1w", "mnr2w", "mnr3w", "mnr4w", "mnr5w", "mnr6w", "mnr7w", "mnr8w"
};
static constexpr const char* kAddressNvs[kMicronovaRegisterCount] = {
    "mnr0a", "mnr1a", "mnr2a", "mnr3a", "mnr4a", "mnr5a", "mnr6a", "mnr7a", "mnr8a"
};
static constexpr const char* kScaleNvs[kMicronovaRegisterCount] = {
    "mnr0s", "mnr1s", "mnr2s", "mnr3s", "mnr4s", "mnr5s", "mnr6s", "mnr7s", "mnr8s"
};
static constexpr const char* kEnabledNvs[kMicronovaRegisterCount] = {
    "mnr0e", "mnr1e", "mnr2e", "mnr3e", "mnr4e", "mnr5e", "mnr6e", "mnr7e", "mnr8e"
};

static bool floatSame(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void copyText(char* dst, size_t len, const char* src)
{
    if (!dst || len == 0U) return;
    if (!src) src = "";
    snprintf(dst, len, "%s", src);
}
}

MicronovaBoilerModule::MicronovaBoilerModule()
    : normalIntervalVar_{NVS_KEY("mn_pnorm"), "normal_interval_ms", "micronova/poll", ConfigType::Int32, &normalIntervalMs_, ConfigPersistence::Persistent, 0},
      fastIntervalVar_{NVS_KEY("mn_pfast"), "fast_interval_ms", "micronova/poll", ConfigType::Int32, &fastIntervalMs_, ConfigPersistence::Persistent, 0},
      fastCyclesVar_{NVS_KEY("mn_pcyc"), "fast_cycles", "micronova/poll", ConfigType::Int32, &fastCyclesCfg_, ConfigPersistence::Persistent, 0},
      powerOnWriteVar_{NVS_KEY("mn_on_wc"), "write_code", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.writeCode, ConfigPersistence::Persistent, 0},
      powerOnAddressVar_{NVS_KEY("mn_on_ad"), "address", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.address, ConfigPersistence::Persistent, 0},
      powerOnValueVar_{NVS_KEY("mn_on_va"), "value", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.value, ConfigPersistence::Persistent, 0},
      powerOnRepeatCountVar_{NVS_KEY("mn_on_rc"), "repeat_count", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.repeatCount, ConfigPersistence::Persistent, 0},
      powerOnRepeatDelayVar_{NVS_KEY("mn_on_rd"), "repeat_delay_ms", "micronova/registers/power_on", ConfigType::Int32, &powerOn_.repeatDelayMs, ConfigPersistence::Persistent, 0},
      powerOffWriteVar_{NVS_KEY("mn_off_wc"), "write_code", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.writeCode, ConfigPersistence::Persistent, 0},
      powerOffAddressVar_{NVS_KEY("mn_off_ad"), "address", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.address, ConfigPersistence::Persistent, 0},
      powerOffValueVar_{NVS_KEY("mn_off_va"), "value", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.value, ConfigPersistence::Persistent, 0},
      powerOffRepeatCountVar_{NVS_KEY("mn_off_rc"), "repeat_count", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.repeatCount, ConfigPersistence::Persistent, 0},
      powerOffRepeatDelayVar_{NVS_KEY("mn_off_rd"), "repeat_delay_ms", "micronova/registers/power_off", ConfigType::Int32, &powerOff_.repeatDelayMs, ConfigPersistence::Persistent, 0}
{
    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const MicronovaRegisterDef& def = kMicronovaDefaultRegisters[i];
        regs_[i].readCode = def.readCode;
        regs_[i].writeCode = def.writeCode;
        regs_[i].address = def.address;
        regs_[i].scale = def.scale;
        regs_[i].offset = def.offset;
        regs_[i].writable = def.writable;
        regs_[i].enabled = def.enabled;

        regReadVars_[i] = {kReadNvs[i], "read_code", kRegModuleNames[i], ConfigType::Int32, &regs_[i].readCode, ConfigPersistence::Persistent, 0};
        regWriteVars_[i] = {kWriteNvs[i], "write_code", kRegModuleNames[i], ConfigType::Int32, &regs_[i].writeCode, ConfigPersistence::Persistent, 0};
        regAddressVars_[i] = {kAddressNvs[i], "address", kRegModuleNames[i], ConfigType::Int32, &regs_[i].address, ConfigPersistence::Persistent, 0};
        regScaleVars_[i] = {kScaleNvs[i], "scale", kRegModuleNames[i], ConfigType::Float, &regs_[i].scale, ConfigPersistence::Persistent, 0};
        regEnabledVars_[i] = {kEnabledNvs[i], "enabled", kRegModuleNames[i], ConfigType::Bool, &regs_[i].enabled, ConfigPersistence::Persistent, 0};
    }

    powerOn_.writeCode = kMicronovaPowerOnDefault.writeCode;
    powerOn_.address = kMicronovaPowerOnDefault.address;
    powerOn_.value = kMicronovaPowerOnDefault.value;
    powerOn_.repeatCount = kMicronovaPowerOnDefault.repeatCount;
    powerOn_.repeatDelayMs = kMicronovaPowerOnDefault.repeatDelayMs;

    powerOff_.writeCode = kMicronovaPowerOffDefault.writeCode;
    powerOff_.address = kMicronovaPowerOffDefault.address;
    powerOff_.value = kMicronovaPowerOffDefault.value;
    powerOff_.repeatCount = kMicronovaPowerOffDefault.repeatCount;
    powerOff_.repeatDelayMs = kMicronovaPowerOffDefault.repeatDelayMs;
}

void MicronovaBoilerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Micronova;
    cfg.registerVar(normalIntervalVar_, kCfgModuleId, 20);
    cfg.registerVar(fastIntervalVar_, kCfgModuleId, 20);
    cfg.registerVar(fastCyclesVar_, kCfgModuleId, 20);

    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const uint8_t branch = (uint8_t)(30U + i);
        cfg.registerVar(regReadVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regWriteVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regAddressVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regScaleVars_[i], kCfgModuleId, branch);
        cfg.registerVar(regEnabledVars_[i], kCfgModuleId, branch);
    }

    cfg.registerVar(powerOnWriteVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnAddressVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnValueVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnRepeatCountVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOnRepeatDelayVar_, kCfgModuleId, 50);
    cfg.registerVar(powerOffWriteVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffAddressVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffValueVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffRepeatCountVar_, kCfgModuleId, 51);
    cfg.registerVar(powerOffRepeatDelayVar_, kCfgModuleId, 51);

    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    if (eventBus_) {
        eventBus_->subscribe(EventId::MicronovaCommandPower, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandPowerLevel, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandFanSpeed, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandTargetTemperature, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandRefresh, &MicronovaBoilerModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaCommandRawWrite, &MicronovaBoilerModule::onEventStatic_, this);
    }
}

void MicronovaBoilerModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    LOGI("Micronova boiler begin deferred");
}

void MicronovaBoilerModule::onStart(ConfigStore&, ServiceRegistry&)
{
    (void)begin();
}

bool MicronovaBoilerModule::begin()
{
    begun_ = bus_ != nullptr;
    nextPollMs_ = millis();
    return begun_;
}

void MicronovaBoilerModule::loop()
{
    tick(millis());
}

const MicronovaBoilerModule::RegisterConfig& MicronovaBoilerModule::reg_(MicronovaRegisterId id) const
{
    return regs_[(uint8_t)id];
}

MicronovaBoilerModule::RegisterConfig& MicronovaBoilerModule::reg_(MicronovaRegisterId id)
{
    return regs_[(uint8_t)id];
}

uint8_t MicronovaBoilerModule::clampLevel_(uint8_t level) const
{
    return level > 5U ? 5U : level;
}

bool MicronovaBoilerModule::queueRegisterRead_(MicronovaRegisterId id)
{
    if (!bus_) return false;
    const RegisterConfig& r = reg_(id);
    if (!r.enabled || r.address < 0) return false;
    return bus_->queueRead((uint8_t)r.readCode, (uint8_t)r.address);
}

void MicronovaBoilerModule::queuePollingSweep_(uint32_t nowMs)
{
    (void)queueRegisterRead_(MicronovaRegisterId::StoveState);
    (void)queueRegisterRead_(MicronovaRegisterId::RoomTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::TargetTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::FumesTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::PowerLevel);
    (void)queueRegisterRead_(MicronovaRegisterId::FanSpeed);
    (void)queueRegisterRead_(MicronovaRegisterId::WaterTemperature);
    (void)queueRegisterRead_(MicronovaRegisterId::WaterPressure);
    (void)queueRegisterRead_(MicronovaRegisterId::AlarmCode);

    uint32_t interval = (normalIntervalMs_ > 0) ? (uint32_t)normalIntervalMs_ : 1800000UL;
    if (fastCyclesRemaining_ > 0U) {
        --fastCyclesRemaining_;
        interval = (fastIntervalMs_ > 0) ? (uint32_t)fastIntervalMs_ : 60000UL;
    }
    nextPollMs_ = nowMs + interval;
}

void MicronovaBoilerModule::syncOnline_(uint32_t)
{
    if (!bus_ || !dataStore_) return;
    const bool online = bus_->isOnline();
    if (online == lastOnline_) return;
    lastOnline_ = online;
    RuntimeData& rt = dataStore_->dataMutable();
    if (rt.micronova.online != online) {
        rt.micronova.online = online;
        dataStore_->notifyChanged(DataKeys::MicronovaOnline);
    }
    if (eventBus_) {
        eventBus_->post(EventId::MicronovaOnlineChanged, nullptr, 0, moduleId());
    }
}

void MicronovaBoilerModule::tick(uint32_t nowMs)
{
    if (!begun_ || !bus_) return;
    syncOnline_(nowMs);

    MicronovaRawValue raw{};
    while (bus_->pollValue(raw)) {
        handleRawValue_(raw);
    }

    if ((int32_t)(nowMs - nextPollMs_) >= 0) {
        queuePollingSweep_(nowMs);
    }
}

void MicronovaBoilerModule::handleRawValue_(const MicronovaRawValue& value)
{
    if (!value.valid) return;
    for (uint8_t i = 0; i < kMicronovaRegisterCount; ++i) {
        const RegisterConfig& r = regs_[i];
        if (!r.enabled) continue;
        if ((uint8_t)r.readCode != value.readCode || (uint8_t)r.address != value.memoryAddress) continue;
        const float converted = ((float)value.value * r.scale) + r.offset;
        publishRuntimeValue_((MicronovaRegisterId)i, converted, value.value, millis());
        return;
    }
}

void MicronovaBoilerModule::publishRuntimeValue_(MicronovaRegisterId id, float converted, int16_t raw, uint32_t nowMs)
{
    if (!dataStore_) return;

    RuntimeData& rt = dataStore_->dataMutable();
    bool changed = false;
    DataKey key = DataKeys::MicronovaLastUpdateMs;

    switch (id) {
        case MicronovaRegisterId::StoveState: {
            const int32_t code = (int32_t)raw;
            const char* stateText = micronovaStoveStateText((uint8_t)code);
            const char* powerText = micronovaPowerStateText(micronovaPowerStateFromStoveState((uint8_t)code));
            if (rt.micronova.stoveStateCode != code) {
                rt.micronova.stoveStateCode = code;
                changed = true;
                key = DataKeys::MicronovaStoveStateCode;
                dataStore_->notifyChanged(DataKeys::MicronovaStoveStateCode);
            }
            if (strncmp(rt.micronova.stoveStateText, stateText, sizeof(rt.micronova.stoveStateText)) != 0) {
                copyText(rt.micronova.stoveStateText, sizeof(rt.micronova.stoveStateText), stateText);
                dataStore_->notifyChanged(DataKeys::MicronovaStoveStateText);
                changed = true;
            }
            if (strncmp(rt.micronova.powerState, powerText, sizeof(rt.micronova.powerState)) != 0) {
                copyText(rt.micronova.powerState, sizeof(rt.micronova.powerState), powerText);
                dataStore_->notifyChanged(DataKeys::MicronovaPowerState);
                changed = true;
            }
            break;
        }
        case MicronovaRegisterId::RoomTemperature:
            if (!floatSame(rt.micronova.roomTemperature, converted)) {
                rt.micronova.roomTemperature = converted;
                key = DataKeys::MicronovaRoomTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::FumesTemperature:
            if (!floatSame(rt.micronova.fumesTemperature, converted)) {
                rt.micronova.fumesTemperature = converted;
                key = DataKeys::MicronovaFumesTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::PowerLevel:
            if (rt.micronova.powerLevel != (int32_t)raw) {
                rt.micronova.powerLevel = raw;
                key = DataKeys::MicronovaPowerLevel;
                changed = true;
            }
            break;
        case MicronovaRegisterId::FanSpeed:
            if (rt.micronova.fanSpeed != (int32_t)raw) {
                rt.micronova.fanSpeed = raw;
                key = DataKeys::MicronovaFanSpeed;
                changed = true;
            }
            break;
        case MicronovaRegisterId::TargetTemperature:
            if (rt.micronova.targetTemperature != (int32_t)raw) {
                rt.micronova.targetTemperature = raw;
                key = DataKeys::MicronovaTargetTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::WaterTemperature:
            if (!floatSame(rt.micronova.waterTemperature, converted)) {
                rt.micronova.waterTemperature = converted;
                key = DataKeys::MicronovaWaterTemperature;
                changed = true;
            }
            break;
        case MicronovaRegisterId::WaterPressure:
            if (!floatSame(rt.micronova.waterPressure, converted)) {
                rt.micronova.waterPressure = converted;
                key = DataKeys::MicronovaWaterPressure;
                changed = true;
            }
            break;
        case MicronovaRegisterId::AlarmCode:
            if (rt.micronova.alarmCode != (int32_t)raw) {
                rt.micronova.alarmCode = raw;
                key = DataKeys::MicronovaAlarmCode;
                changed = true;
            }
            break;
        case MicronovaRegisterId::Count:
            break;
    }

    if (changed) {
        if (key != DataKeys::MicronovaStoveStateCode) {
            dataStore_->notifyChanged(key);
        }
        MicronovaValueUpdatedPayload payload{};
        snprintf(payload.key, sizeof(payload.key), "%s", kMicronovaDefaultRegisters[(uint8_t)id].key);
        payload.value = converted;
        payload.raw = raw;
        if (eventBus_) {
            eventBus_->post(EventId::MicronovaValueUpdated, &payload, sizeof(payload), moduleId());
        }
    }

    if (rt.micronova.lastUpdateMs != nowMs) {
        rt.micronova.lastUpdateMs = nowMs;
        dataStore_->notifyChanged(DataKeys::MicronovaLastUpdateMs);
    }
}

bool MicronovaBoilerModule::writeCommand_(const CommandConfig& command)
{
    if (!bus_) return false;
    const uint8_t repeat = command.repeatCount <= 0 ? 1U : (uint8_t)command.repeatCount;
    const uint16_t repeatDelay = command.repeatDelayMs <= 0 ? MicronovaProtocol::DefaultRepeatDelayMs : (uint16_t)command.repeatDelayMs;
    return bus_->queueWrite((uint8_t)command.writeCode,
                            (uint8_t)command.address,
                            (uint8_t)command.value,
                            repeat,
                            repeatDelay);
}

bool MicronovaBoilerModule::writeRegister_(MicronovaRegisterId id, uint8_t value)
{
    if (!bus_) return false;
    const RegisterConfig& r = reg_(id);
    if (!r.enabled || !r.writable) return false;
    return bus_->queueWrite((uint8_t)r.writeCode, (uint8_t)r.address, value, 1, MicronovaProtocol::DefaultRepeatDelayMs);
}

void MicronovaBoilerModule::recordLastCommand_(const char* command)
{
    if (dataStore_) {
        RuntimeData& rt = dataStore_->dataMutable();
        if (strncmp(rt.micronova.lastCommand, command, sizeof(rt.micronova.lastCommand)) != 0) {
            copyText(rt.micronova.lastCommand, sizeof(rt.micronova.lastCommand), command);
            dataStore_->notifyChanged(DataKeys::MicronovaLastCommand);
        }
    }
    fastCyclesRemaining_ = fastCyclesCfg_ > 0 ? (uint16_t)fastCyclesCfg_ : 30U;
    nextPollMs_ = millis();
}

bool MicronovaBoilerModule::setPower(bool on)
{
    const bool ok = writeCommand_(on ? powerOn_ : powerOff_);
    if (ok) recordLastCommand_(on ? "power_on" : "power_off");
    return ok;
}

bool MicronovaBoilerModule::setPowerLevel(uint8_t level)
{
    const bool ok = writeRegister_(MicronovaRegisterId::PowerLevel, clampLevel_(level));
    if (ok) recordLastCommand_("power_level");
    return ok;
}

bool MicronovaBoilerModule::setFanSpeed(uint8_t level)
{
    const bool ok = writeRegister_(MicronovaRegisterId::FanSpeed, clampLevel_(level));
    if (ok) recordLastCommand_("fan_speed");
    return ok;
}

bool MicronovaBoilerModule::setTargetTemperature(uint8_t temperature)
{
    const bool ok = writeRegister_(MicronovaRegisterId::TargetTemperature, temperature);
    if (ok) recordLastCommand_("target_temperature");
    return ok;
}

bool MicronovaBoilerModule::refreshNow()
{
    fastCyclesRemaining_ = fastCyclesCfg_ > 0 ? (uint16_t)fastCyclesCfg_ : 30U;
    nextPollMs_ = millis();
    recordLastCommand_("refresh");
    return true;
}

void MicronovaBoilerModule::onEventStatic_(const Event& e, void* user)
{
    MicronovaBoilerModule* self = static_cast<MicronovaBoilerModule*>(user);
    if (self) self->handleCommandEvent_(e);
}

void MicronovaBoilerModule::handleCommandEvent_(const Event& e)
{
    switch (e.id) {
        case EventId::MicronovaCommandPower: {
            if (e.len < sizeof(MicronovaCommandPowerPayload) || !e.payload) return;
            const MicronovaCommandPowerPayload* p = (const MicronovaCommandPowerPayload*)e.payload;
            (void)setPower(p->on != 0U);
            break;
        }
        case EventId::MicronovaCommandPowerLevel: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            (void)setPowerLevel(p->value);
            break;
        }
        case EventId::MicronovaCommandFanSpeed: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            (void)setFanSpeed(p->value);
            break;
        }
        case EventId::MicronovaCommandTargetTemperature: {
            if (e.len < sizeof(MicronovaCommandValuePayload) || !e.payload) return;
            const MicronovaCommandValuePayload* p = (const MicronovaCommandValuePayload*)e.payload;
            (void)setTargetTemperature(p->value);
            break;
        }
        case EventId::MicronovaCommandRefresh:
            (void)refreshNow();
            break;
        case EventId::MicronovaCommandRawWrite: {
            if (e.len < sizeof(MicronovaCommandRawWritePayload) || !e.payload || !bus_) return;
            const MicronovaCommandRawWritePayload* p = (const MicronovaCommandRawWritePayload*)e.payload;
            (void)bus_->queueWrite(p->writeCode, p->address, p->value, p->repeatCount, p->repeatDelayMs);
            recordLastCommand_("raw_write");
            break;
        }
        default:
            break;
    }
}
