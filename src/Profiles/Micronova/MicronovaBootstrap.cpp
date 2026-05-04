#include "Profiles/Micronova/MicronovaProfile.h"
#include "Profiles/Micronova/MicronovaIoAssembly.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "App/AppContext.h"
#include "Board/BoardSerialMap.h"
#include "Core/ConfigMigrations.h"
#include "Core/DataStore/DataStore.h"
#include "Core/MqttTopics.h"
#include "Core/NvsKeys.h"
#include "Core/Services/IHA.h"
#include "Core/SnprintfCheck.h"
#include "Core/SystemStats.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"

#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

namespace {

using Profiles::Micronova::ModuleInstances;

void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Serial.printf("Micronova setup failure: %s\r\n", step ? step : "unknown");
    while (true) delay(1000);
}

bool buildNetworkSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = wifiIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    const bool netReady = wifiReady(*ds);
    const bool mqttOk = mqttReady(*ds);
    const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
    uint8_t mac[6] = {0};
    char macText[18] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(macText,
             sizeof(macText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)mac[0],
             (unsigned)mac[1],
             (unsigned)mac[2],
             (unsigned)mac[3],
             (unsigned)mac[4],
             (unsigned)mac[5]);

    const int wrote = snprintf(out,
                               len,
                               "{\"ready\":%s,\"ip\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                               netReady ? "true" : "false",
                               ip,
                               macText,
                               rssi,
                               mqttOk ? "true" : "false",
                               (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len)
{
    if (!mqtt || !out || len == 0) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    DataStore* ds = mqtt->dataStorePtr();
    const uint32_t rxDrop = ds ? mqttRxDrop(*ds) : 0U;
    const uint32_t parseFail = ds ? mqttParseFail(*ds) : 0U;
    const uint32_t handlerFail = ds ? mqttHandlerFail(*ds) : 0U;
    const uint32_t oversizeDrop = ds ? mqttOversizeDrop(*ds) : 0U;

    const int wrote = snprintf(
        out,
        len,
        "{\"upt_ms\":%llu,\"heap\":{\"free\":%lu,\"min_free\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"mqtt_rx\":{\"rx_drop\":%lu,\"oversize_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu},\"ts\":%lu}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)rxDrop,
        (unsigned long)oversizeDrop,
        (unsigned long)parseFail,
        (unsigned long)handlerFail,
        (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

void registerMicronovaHomeAssistant(AppContext& ctx, ModuleInstances& modules)
{
    const HAService* ha = ctx.services.get<HAService>(ServiceId::Ha);
    if (!ha) return;

    static constexpr const char* kRawValueTpl = "{{ value }}";
    static constexpr const char* kIntTpl = "{{ value | int(-1) }}";
    static constexpr const char* kFloatTpl = "{{ value | float(0) }}";
    static constexpr const char* kIoFloatTpl =
        "{% if value_json.value is number %}{{ value_json.value | float | round(1) }}{% else %}unavailable{% endif %}";
    static constexpr const char* kIoBoolSwitchTpl = "{% if value_json.value %}ON{% else %}OFF{% endif %}";
    static constexpr const char* kIoAvailabilityTpl = "{{ 'online' if value_json.available else 'offline' }}";

    if (ha->addBinarySensor) {
        const HABinarySensorEntry connection{
            "micronova",
            "mn_connection",
            "Boiler Connection",
            "micronova/status/connection",
            "{{ 'True' if value == 'online' else 'False' }}",
            "connectivity",
            nullptr,
            "mdi:connection"
        };
        (void)ha->addBinarySensor(ha->ctx, &connection);
    }

    if (ha->addSensor) {
        const HASensorEntry sensors[] = {
            {"micronova", "mn_state", "Boiler State", "micronova/status/stove_state", kRawValueTpl, nullptr, "mdi:fireplace", nullptr, false, nullptr, true},
            {"micronova", "mn_state_code", "Boiler State Code", "micronova/status/stove_state_code", kIntTpl, "diagnostic", "mdi:numeric", nullptr, false, nullptr, false},
            {"micronova", "mn_alarm_code", "Alarm Code", "micronova/status/alarm_code", kIntTpl, "diagnostic", "mdi:alert-outline", nullptr, false, nullptr, false},
            {"micronova", "mn_room_temp", "Room Temperature", "micronova/sensor/ambtemp", kFloatTpl, nullptr, "mdi:home-thermometer-outline", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_fumes_temp", "Fumes Temperature", "micronova/sensor/fumetemp", kFloatTpl, nullptr, "mdi:smoke", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_water_temp", "Water Temperature", "micronova/sensor/water_temperature", kFloatTpl, nullptr, "mdi:water-thermometer", "\xC2\xB0""C", false, nullptr, false},
            {"micronova", "mn_water_pressure", "Water Pressure", "micronova/sensor/water_pressure", kFloatTpl, nullptr, "mdi:gauge", "bar", false, nullptr, false},
            {"micronova", "mn_last_command", "Last Command", "micronova/status/last_command", kRawValueTpl, "diagnostic", "mdi:console", nullptr, false, nullptr, true},
            {"micronova", "mn_local_temp", "Local Temperature", "rt/io/input/a00", kIoFloatTpl, nullptr, "mdi:thermometer", "\xC2\xB0""C", false, kIoAvailabilityTpl, false},
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(sensors) / sizeof(sensors[0])); ++i) {
            (void)ha->addSensor(ha->ctx, &sensors[i]);
        }
    }

    if (ha->addSwitch) {
        const HASwitchEntry power{
            "micronova",
            "mn_power",
            "Power",
            "micronova/status/onoff",
            kRawValueTpl,
            "micronova/command/power/set",
            "ON",
            "OFF",
            "mdi:power",
            nullptr
        };
        const HASwitchEntry auxOutput{
            "micronova",
            "mn_aux_output",
            "Aux Output",
            "rt/io/output/d00",
            kIoBoolSwitchTpl,
            "micronova/io/output/d00/set",
            "ON",
            "OFF",
            "mdi:electric-switch",
            nullptr
        };
        (void)ha->addSwitch(ha->ctx, &power);
        (void)ha->addSwitch(ha->ctx, &auxOutput);
    }

    if (ha->addNumber) {
        const HANumberEntry numbers[] = {
            {"micronova", "mn_power_level", "Power Level", "micronova/sensor/power", kIntTpl, "micronova/command/power_level/set", "{{ value | int }}", 0.0f, 5.0f, 1.0f, "slider", nullptr, "mdi:fire", nullptr},
            {"micronova", "mn_fan_speed", "Fan Speed", "micronova/sensor/fan", kIntTpl, "micronova/command/fan/set", "{{ value | int }}", 0.0f, 5.0f, 1.0f, "slider", nullptr, "mdi:fan", nullptr},
            {"micronova", "mn_target_temp", "Target Temperature", "micronova/sensor/tempset", kIntTpl, "micronova/command/temperature/set", "{{ value | int }}", 5.0f, 35.0f, 1.0f, "box", nullptr, "mdi:thermometer-lines", "\xC2\xB0""C"},
        };
        for (uint8_t i = 0; i < (uint8_t)(sizeof(numbers) / sizeof(numbers[0])); ++i) {
            (void)ha->addNumber(ha->ctx, &numbers[i]);
        }
    }

    if (ha->addButton) {
        const HAButtonEntry refresh{
            "micronova",
            "mn_refresh",
            "Refresh Boiler",
            "micronova/command/refresh",
            "1",
            "diagnostic",
            "mdi:refresh"
        };
        (void)ha->addButton(ha->ctx, &refresh);
    }

    if (ha->requestRefresh) {
        (void)ha->requestRefresh(ha->ctx);
    }
    (void)modules;
}

void postInit(AppContext& ctx, ModuleInstances& modules)
{
    modules.mqttModule.formatTopic(modules.topicNetworkState, sizeof(modules.topicNetworkState), "rt/network/state");
    modules.mqttModule.formatTopic(modules.topicSystemState, sizeof(modules.topicSystemState), "rt/system/state");
    modules.mqttModule.addRuntimePublisher(modules.topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    modules.mqttModule.addRuntimePublisher(modules.topicSystemState, 60000, 0, false, buildSystemSnapshot);
    registerMicronovaHomeAssistant(ctx, modules);
}

}  // namespace

namespace Profiles {
namespace Micronova {

void setupProfile(AppContext& ctx)
{
    ModuleInstances& modules = moduleInstances();

    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);
    modules.haModule.setBranding("pio", "Pellet.io", "Pellet.io", "Pellet Controller");

    ctx.preferences.begin(NvsKeys::StorageNamespace, false);
    ctx.registry.setPreferences(ctx.preferences);
    ctx.registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);

    ctx.moduleManager.add(&modules.logHubModule);
    ctx.moduleManager.add(&modules.logDispatcherModule);
    ctx.moduleManager.add(&modules.logSerialSinkModule);
    ctx.moduleManager.add(&modules.eventBusModule);

    ctx.moduleManager.add(&modules.configStoreModule);
    ctx.moduleManager.add(&modules.dataStoreModule);
    ctx.moduleManager.add(&modules.commandModule);
    ctx.moduleManager.add(&modules.alarmModule);
    ctx.moduleManager.add(&modules.wifiModule);
    ctx.moduleManager.add(&modules.wifiProvisioningModule);
    ctx.moduleManager.add(&modules.timeModule);
    ctx.moduleManager.add(&modules.mqttModule);
    ctx.moduleManager.add(&modules.haModule);
    ctx.moduleManager.add(&modules.ioModule);
    ctx.moduleManager.add(&modules.webInterfaceModule);
    ctx.moduleManager.add(&modules.micronovaBusModule);
    ctx.moduleManager.add(&modules.micronovaBoilerModule);
    ctx.moduleManager.add(&modules.micronovaMqttBridgeModule);
    ctx.moduleManager.add(&modules.systemModule);

    modules.systemMonitorModule.setModuleManager(&ctx.moduleManager);
    ctx.moduleManager.add(&modules.systemMonitorModule);

    requireSetup(ctx.board != nullptr, "missing board spec");
    requireSetup(configureIoModule(*ctx.board, modules), "configure Micronova IO");
    requireSetup(modules.mqttModule.registerRuntimeProvider(&modules.ioModule), "register runtime provider io");
    requireSetup(ctx.moduleManager.initAll(ctx.registry, ctx.services), "init modules");
    postInit(ctx, modules);
}

void loopProfile(AppContext&)
{
    delay(20);
}

}  // namespace Micronova
}  // namespace Profiles
