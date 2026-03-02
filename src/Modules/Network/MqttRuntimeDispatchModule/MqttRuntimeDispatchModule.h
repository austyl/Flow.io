#pragma once
/**
 * @file MqttRuntimeDispatchModule.h
 * @brief Runtime dispatch module using a reusable dispatch core and a MQTT sink.
 */

#include "Core/Module.h"
#include "Core/RuntimeDispatchCore.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/SystemLimits.h"
#include "Core/Services/Services.h"

class MqttRuntimeDispatchModule : public Module {
public:
    const char* moduleId() const override { return "rt.dispatch.mqtt"; }
    const char* taskName() const override { return "rt.mqtt"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 3072; }

    uint8_t dependencyCount() const override { return 4; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "eventbus";
        if (i == 2) return "datastore";
        if (i == 3) return "mqtt";
        return nullptr;
    }

    bool registerProvider(const IRuntimeSnapshotProvider* provider);

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    class MqttRuntimeSink : public IRuntimeDispatchSink {
    public:
        void configure(const MqttService* mqttSvc, const DataStore* dataStore);

        bool resolveRouteTarget(const char* suffix, char* out, size_t outLen) override;
        bool canPublish() const override;
        bool publish(const char* routeTarget, const char* payload) override;
        bool onDataChanged(const DataChangedPayload& change) override;

    private:
        const MqttService* mqttSvc_ = nullptr;
        const DataStore* dataStore_ = nullptr;
        bool mqttReadyCached_ = false;
    };

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);

    const LogHubService* logHub_ = nullptr;
    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;

    RuntimeDispatchCore core_{};
    MqttRuntimeSink sink_{};

    char publishBuf_[Limits::Mqtt::Buffers::Publish] = {0};
};
