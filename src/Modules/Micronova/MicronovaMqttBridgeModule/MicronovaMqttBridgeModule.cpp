#include "Modules/Micronova/MicronovaMqttBridgeModule/MicronovaMqttBridgeModule.h"

#include "Core/DataKeys.h"
#include "Core/Services/Services.h"
#include "Modules/Micronova/MicronovaBoilerModule/MicronovaBoilerModuleDataModel.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MicronovaMqttBridgeModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr const char* kSuffixConnection = "micronova/status/connection";
static constexpr const char* kSuffixState = "micronova/status/state";
static constexpr const char* kSuffixOnoff = "micronova/status/onoff";
static constexpr const char* kSuffixPowerState = "micronova/status/power_state";
static constexpr const char* kSuffixPowerLevel = "micronova/status/power_level";
static constexpr const char* kSuffixStoveState = "micronova/status/stove_state";
static constexpr const char* kSuffixStoveStateCode = "micronova/status/stove_state_code";
static constexpr const char* kSuffixAlarmCode = "micronova/status/alarm_code";
static constexpr const char* kSuffixLastCommand = "micronova/status/last_command";
static constexpr const char* kSuffixRoomTemperature = "micronova/sensor/ambtemp";
static constexpr const char* kSuffixFumesTemperature = "micronova/sensor/fumetemp";
static constexpr const char* kSuffixWaterTemperature = "micronova/sensor/water_temperature";
static constexpr const char* kSuffixWaterPressure = "micronova/sensor/water_pressure";
static constexpr const char* kSuffixPowerSensor = "micronova/sensor/power";
static constexpr const char* kSuffixFanSensor = "micronova/sensor/fan";
static constexpr const char* kSuffixTargetTemperature = "micronova/sensor/tempset";

static constexpr const char* kCommandPower = "micronova/command/power/set";
static constexpr const char* kCommandPowerLevel = "micronova/command/power_level/set";
static constexpr const char* kCommandFan = "micronova/command/fan/set";
static constexpr const char* kCommandTemperature = "micronova/command/temperature/set";
static constexpr const char* kCommandRefresh = "micronova/command/refresh";
static constexpr const char* kCommandCompact = "micronova/command/cmd";
static constexpr const char* kCommandAuxOutput = "micronova/io/output/d00/set";
static constexpr const char* kCommandAuxOutputAlias = "micronova/command/aux_output/set";

static const char* trim(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0U) return "";
    out[0] = '\0';
    if (!in) return out;
    while (*in && isspace((unsigned char)*in)) ++in;
    size_t n = strnlen(in, outLen - 1U);
    while (n > 0U && isspace((unsigned char)in[n - 1U])) --n;
    memcpy(out, in, n);
    out[n] = '\0';
    return out;
}

static bool equalsIgnoreCase(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}
}

void MicronovaMqttBridgeModule::init(ConfigStore&, ServiceRegistry& services)
{
    const EventBusService* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    mqttSvc_ = services.get<MqttService>(ServiceId::Mqtt);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);

    producer_ = MqttPublishProducer{};
    producer_.producerId = ProducerId;
    producer_.ctx = this;
    producer_.buildMessage = &MicronovaMqttBridgeModule::buildMessageStatic_;

    if (mqttSvc_ && mqttSvc_->registerProducer) {
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }
    registerInbound_();

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &MicronovaMqttBridgeModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaValueUpdated, &MicronovaMqttBridgeModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::MicronovaOnlineChanged, &MicronovaMqttBridgeModule::onEventStatic_, this);
    }
}

void MicronovaMqttBridgeModule::registerInbound_()
{
    if (inboundRegistered_ || !mqttSvc_ || !mqttSvc_->registerInboundHandler) return;
    static constexpr const char* kTopics[InboundCount] = {
        kCommandPower,
        kCommandPowerLevel,
        kCommandFan,
        kCommandTemperature,
        kCommandRefresh,
        kCommandCompact,
        kCommandAuxOutput,
        kCommandAuxOutputAlias
    };
    bool ok = true;
    for (uint8_t i = 0; i < InboundCount; ++i) {
        inbound_[i] = MqttInboundHandler{kTopics[i], this, &MicronovaMqttBridgeModule::onInboundStatic_};
        ok = mqttSvc_->registerInboundHandler(mqttSvc_->ctx, &inbound_[i]) && ok;
    }
    inboundRegistered_ = ok;
}

void MicronovaMqttBridgeModule::onEventStatic_(const Event& e, void* user)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(user);
    if (self) self->onEvent_(e);
}

void MicronovaMqttBridgeModule::onEvent_(const Event& e)
{
    if (e.id == EventId::MicronovaValueUpdated || e.id == EventId::MicronovaOnlineChanged) {
        enqueueAll_(MqttPublishPriority::Normal);
        return;
    }
    if (e.id != EventId::DataChanged || e.len < sizeof(DataChangedPayload) || !e.payload) return;
    const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
    if (p->id == DATAKEY_MQTT_READY) {
        if (dataStore_ && mqttReady(*dataStore_)) enqueueAll_(MqttPublishPriority::High);
        return;
    }
    enqueueForKey_(p->id);
}

void MicronovaMqttBridgeModule::enqueueAll_(MqttPublishPriority priority)
{
    if (!producerRegistered_ || !mqttSvc_ || !mqttSvc_->enqueue) return;
    for (uint16_t msg = MsgConnection; msg < MsgCount; ++msg) {
        (void)mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, msg, (uint8_t)priority, 0);
    }
}

void MicronovaMqttBridgeModule::enqueueForKey_(DataKey key)
{
    if (!producerRegistered_ || !mqttSvc_ || !mqttSvc_->enqueue) return;
    uint16_t msgs[4] = {0};
    uint8_t n = 0;
    switch (key) {
        case DataKeys::MicronovaOnline: msgs[n++] = MsgConnection; break;
        case DataKeys::MicronovaStoveStateText: msgs[n++] = MsgState; msgs[n++] = MsgStoveState; break;
        case DataKeys::MicronovaStoveStateCode: msgs[n++] = MsgStoveStateCode; break;
        case DataKeys::MicronovaPowerState: msgs[n++] = MsgOnoff; msgs[n++] = MsgPowerState; break;
        case DataKeys::MicronovaPowerLevel: msgs[n++] = MsgPowerLevel; msgs[n++] = MsgPowerSensor; break;
        case DataKeys::MicronovaFanSpeed: msgs[n++] = MsgFanSensor; break;
        case DataKeys::MicronovaTargetTemperature: msgs[n++] = MsgTargetTemperature; break;
        case DataKeys::MicronovaRoomTemperature: msgs[n++] = MsgRoomTemperature; break;
        case DataKeys::MicronovaFumesTemperature: msgs[n++] = MsgFumesTemperature; break;
        case DataKeys::MicronovaWaterTemperature: msgs[n++] = MsgWaterTemperature; break;
        case DataKeys::MicronovaWaterPressure: msgs[n++] = MsgWaterPressure; break;
        case DataKeys::MicronovaAlarmCode: msgs[n++] = MsgAlarmCode; break;
        case DataKeys::MicronovaLastCommand: msgs[n++] = MsgLastCommand; break;
        default: return;
    }
    for (uint8_t i = 0; i < n; ++i) {
        (void)mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, msgs[i], (uint8_t)MqttPublishPriority::High, 0);
    }
}

MqttBuildResult MicronovaMqttBridgeModule::buildMessageStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(ctx);
    return self ? self->buildMessage_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

bool MicronovaMqttBridgeModule::publishText_(MqttBuildContext& ctx, const char* suffix, const char* text, bool retain)
{
    if (!mqttSvc_ || !mqttSvc_->formatTopic || !suffix || !text) return false;
    mqttSvc_->formatTopic(mqttSvc_->ctx, suffix, ctx.topic, ctx.topicCapacity);
    const int pw = snprintf(ctx.payload, ctx.payloadCapacity, "%s", text);
    if (!(pw >= 0 && (uint16_t)pw < ctx.payloadCapacity)) return false;
    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
    ctx.payloadLen = (uint16_t)pw;
    ctx.qos = 0;
    ctx.retain = retain;
    return ctx.topicLen > 0U;
}

bool MicronovaMqttBridgeModule::publishInt_(MqttBuildContext& ctx, const char* suffix, int32_t value, bool retain)
{
    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%ld", (long)value);
    return publishText_(ctx, suffix, buf, retain);
}

bool MicronovaMqttBridgeModule::publishFloat_(MqttBuildContext& ctx, const char* suffix, float value, bool retain)
{
    char buf[24] = {0};
    snprintf(buf, sizeof(buf), "%.2f", (double)value);
    return publishText_(ctx, suffix, buf, retain);
}

MqttBuildResult MicronovaMqttBridgeModule::buildMessage_(uint16_t messageId, MqttBuildContext& ctx)
{
    if (!dataStore_) return MqttBuildResult::RetryLater;
    const MicronovaRuntimeData& rt = dataStore_->data().micronova;
    bool ok = false;
    switch (messageId) {
        case MsgConnection: ok = publishText_(ctx, kSuffixConnection, rt.online ? "online" : "offline", true); break;
        case MsgState: ok = publishText_(ctx, kSuffixState, rt.stoveStateText, true); break;
        case MsgOnoff: ok = publishText_(ctx, kSuffixOnoff, rt.powerState, true); break;
        case MsgPowerState: ok = publishText_(ctx, kSuffixPowerState, rt.powerState, true); break;
        case MsgPowerLevel: ok = publishInt_(ctx, kSuffixPowerLevel, rt.powerLevel, true); break;
        case MsgStoveState: ok = publishText_(ctx, kSuffixStoveState, rt.stoveStateText, true); break;
        case MsgStoveStateCode: ok = publishInt_(ctx, kSuffixStoveStateCode, rt.stoveStateCode, true); break;
        case MsgAlarmCode: ok = publishInt_(ctx, kSuffixAlarmCode, rt.alarmCode, true); break;
        case MsgLastCommand: ok = publishText_(ctx, kSuffixLastCommand, rt.lastCommand, true); break;
        case MsgRoomTemperature: ok = publishFloat_(ctx, kSuffixRoomTemperature, rt.roomTemperature, true); break;
        case MsgFumesTemperature: ok = publishFloat_(ctx, kSuffixFumesTemperature, rt.fumesTemperature, true); break;
        case MsgWaterTemperature: ok = publishFloat_(ctx, kSuffixWaterTemperature, rt.waterTemperature, true); break;
        case MsgWaterPressure: ok = publishFloat_(ctx, kSuffixWaterPressure, rt.waterPressure, true); break;
        case MsgPowerSensor: ok = publishInt_(ctx, kSuffixPowerSensor, rt.powerLevel, true); break;
        case MsgFanSensor: ok = publishInt_(ctx, kSuffixFanSensor, rt.fanSpeed, true); break;
        case MsgTargetTemperature: ok = publishInt_(ctx, kSuffixTargetTemperature, rt.targetTemperature, true); break;
        default: return MqttBuildResult::NoLongerNeeded;
    }
    return ok ? MqttBuildResult::Ready : MqttBuildResult::PermanentError;
}

void MicronovaMqttBridgeModule::onInboundStatic_(void* ctx, const MqttInboundMessage& message)
{
    MicronovaMqttBridgeModule* self = static_cast<MicronovaMqttBridgeModule*>(ctx);
    if (self) self->onInbound_(message);
}

bool MicronovaMqttBridgeModule::postValueCommand_(EventId eventId, uint8_t value)
{
    if (!eventBus_) return false;
    MicronovaCommandValuePayload payload{value};
    return eventBus_->post(eventId, &payload, sizeof(payload), moduleId());
}

bool MicronovaMqttBridgeModule::setAuxOutput_(bool on)
{
    if (!ioSvc_ || !ioSvc_->writeDigital) return false;
    return ioSvc_->writeDigital(ioSvc_->ctx, (IoId)(IO_ID_DO_BASE + 0), on ? 1U : 0U, millis()) == IO_OK;
}

int MicronovaMqttBridgeModule::parseInt_(const char* payload, bool& ok)
{
    ok = false;
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (buf[0] == '\0') return 0;
    char* end = nullptr;
    const long v = strtol(buf, &end, 10);
    if (end == buf || (end && *end != '\0')) return 0;
    ok = true;
    return (int)v;
}

bool MicronovaMqttBridgeModule::parsePower_(const char* payload, bool& out)
{
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (equalsIgnoreCase(buf, "ON") || strcmp(buf, "1") == 0 || equalsIgnoreCase(buf, "true")) {
        out = true;
        return true;
    }
    if (equalsIgnoreCase(buf, "OFF") || strcmp(buf, "0") == 0 || equalsIgnoreCase(buf, "false")) {
        out = false;
        return true;
    }
    return false;
}

bool MicronovaMqttBridgeModule::parseCompact_(const char* payload)
{
    if (!eventBus_) return false;
    char buf[16] = {0};
    trim(payload, buf, sizeof(buf));
    if (buf[0] == '\0') return false;
    for (char* p = buf; *p; ++p) *p = (char)toupper((unsigned char)*p);

    if (strcmp(buf, "ON") == 0) {
        MicronovaCommandPowerPayload p{1};
        return eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
    }
    if (strcmp(buf, "OFF") == 0 || strcmp(buf, "E") == 0) {
        MicronovaCommandPowerPayload p{0};
        return eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
    }
    if ((buf[0] == 'P' || buf[0] == 'F' || buf[0] == 'T') && buf[1] != '\0') {
        bool ok = false;
        const int value = parseInt_(buf + 1, ok);
        if (!ok) return false;
        if (buf[0] == 'P') return postValueCommand_(EventId::MicronovaCommandPowerLevel, (uint8_t)value);
        if (buf[0] == 'F') return postValueCommand_(EventId::MicronovaCommandFanSpeed, (uint8_t)value);
        return postValueCommand_(EventId::MicronovaCommandTargetTemperature, (uint8_t)value);
    }
    return false;
}

void MicronovaMqttBridgeModule::onInbound_(const MqttInboundMessage& message)
{
    if (!eventBus_ || !message.topic || !message.payload || message.payload[0] == '\0') return;

    char topic[Limits::Mqtt::Buffers::Topic] = {0};
    if (mqttSvc_ && mqttSvc_->formatTopic) {
        mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandCompact, topic, sizeof(topic));
    }
    if (topic[0] != '\0' && strcmp(message.topic, topic) == 0) {
        (void)parseCompact_(message.payload);
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandAuxOutput, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        bool on = false;
        if (parsePower_(message.payload, on)) (void)setAuxOutput_(on);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandAuxOutputAlias, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        bool on = false;
        if (parsePower_(message.payload, on)) (void)setAuxOutput_(on);
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPower, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        bool on = false;
        if (parsePower_(message.payload, on)) {
            MicronovaCommandPowerPayload p{(uint8_t)(on ? 1U : 0U)};
            (void)eventBus_->post(EventId::MicronovaCommandPower, &p, sizeof(p), moduleId());
        }
        return;
    }

    bool ok = false;
    const int value = parseInt_(message.payload, ok);
    if (!ok) {
        if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandRefresh, topic, sizeof(topic));
        if (strcmp(message.topic, topic) == 0) {
            (void)eventBus_->post(EventId::MicronovaCommandRefresh, nullptr, 0, moduleId());
        }
        return;
    }

    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandPowerLevel, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        (void)postValueCommand_(EventId::MicronovaCommandPowerLevel, (uint8_t)value);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandFan, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        (void)postValueCommand_(EventId::MicronovaCommandFanSpeed, (uint8_t)value);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandTemperature, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        (void)postValueCommand_(EventId::MicronovaCommandTargetTemperature, (uint8_t)value);
        return;
    }
    if (mqttSvc_ && mqttSvc_->formatTopic) mqttSvc_->formatTopic(mqttSvc_->ctx, kCommandRefresh, topic, sizeof(topic));
    if (strcmp(message.topic, topic) == 0) {
        (void)eventBus_->post(EventId::MicronovaCommandRefresh, nullptr, 0, moduleId());
    }
}
