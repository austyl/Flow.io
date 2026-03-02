/**
 * @file MqttRuntimeDispatchModule.cpp
 * @brief Runtime dispatch module using a reusable dispatch core and a MQTT sink.
 */

#include "MqttRuntimeDispatchModule.h"

#include "Core/DataKeys.h"

#include <Arduino.h>
#include <stdio.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::Core)
#include "Core/ModuleLog.h"

void MqttRuntimeDispatchModule::MqttRuntimeSink::configure(const MqttService* mqttSvc, const DataStore* dataStore)
{
    mqttSvc_ = mqttSvc;
    dataStore_ = dataStore;
    mqttReadyCached_ = (dataStore_ != nullptr) ? dataStore_->data().mqtt.mqttReady : false;
}

bool MqttRuntimeDispatchModule::MqttRuntimeSink::resolveRouteTarget(const char* suffix, char* out, size_t outLen)
{
    if (!suffix || !out || outLen == 0) return false;

    if (mqttSvc_ && mqttSvc_->formatTopic) {
        mqttSvc_->formatTopic(mqttSvc_->ctx, suffix, out, outLen);
        return out[0] != '\0';
    }

    const int wrote = snprintf(out, outLen, "%s", suffix);
    return wrote > 0 && (size_t)wrote < outLen;
}

bool MqttRuntimeDispatchModule::MqttRuntimeSink::canPublish() const
{
    if (!mqttSvc_ || !mqttSvc_->isConnected) return false;
    return mqttSvc_->isConnected(mqttSvc_->ctx);
}

bool MqttRuntimeDispatchModule::MqttRuntimeSink::publish(const char* routeTarget, const char* payload)
{
    if (!mqttSvc_ || !mqttSvc_->publish || !routeTarget || !payload) return false;
    return mqttSvc_->publish(mqttSvc_->ctx, routeTarget, payload, 0, false);
}

bool MqttRuntimeDispatchModule::MqttRuntimeSink::onDataChanged(const DataChangedPayload& change)
{
    if (change.id != DataKeys::MqttReady) return false;

    const bool ready = (dataStore_ != nullptr) ? dataStore_->data().mqtt.mqttReady : false;
    const bool becameReady = ready && !mqttReadyCached_;
    mqttReadyCached_ = ready;
    return becameReady;
}

bool MqttRuntimeDispatchModule::registerProvider(const IRuntimeSnapshotProvider* provider)
{
    return core_.registerProvider(provider);
}

void MqttRuntimeDispatchModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    (void)logHub_;

    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    mqttSvc_ = services.get<MqttService>("mqtt");

    sink_.configure(mqttSvc_, dataStore_);
    core_.setSink(&sink_);

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &MqttRuntimeDispatchModule::onEventStatic_, this);
    }
}

void MqttRuntimeDispatchModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    core_.onConfigLoaded();
    LOGI("Runtime dispatcher ready routes=%u", (unsigned)core_.routeCount());
}

void MqttRuntimeDispatchModule::onEventStatic_(const Event& e, void* user)
{
    MqttRuntimeDispatchModule* self = static_cast<MqttRuntimeDispatchModule*>(user);
    if (!self) return;
    self->onEvent_(e);
}

void MqttRuntimeDispatchModule::onEvent_(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;

    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (!p) return;

    core_.onDataChanged(*p);
}

void MqttRuntimeDispatchModule::loop()
{
    core_.tick(millis(), publishBuf_, sizeof(publishBuf_));
    vTaskDelay(pdMS_TO_TICKS(25));
}
