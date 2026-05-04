#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file SystemLimits.h
 * @brief Shared compile-time limits used across Core and modules.
 */

namespace Limits {

/** @brief MQTT full topic buffer length used by runtime snapshot publications in `main.cpp`. */
constexpr size_t TopicBuf = 70;
/** @brief JSON capacity for MQTT cfg patch parsing in `MQTTModule::publishConfigBlocksFromPatch`. */
constexpr size_t JsonPatchBuf = 1024;
/** @brief JSON capacity for MQTT `cmd` payload parsing in `MQTTModule::processRxCmd_`. */
constexpr size_t JsonCmdBuf = 1024;
/** @brief JSON capacity for MQTT `cfg/set` payload parsing in `MQTTModule::processRxCfgSet_`. */
constexpr size_t JsonCfgBuf = 1024;
/** @brief JSON capacity for command args parsing in `TimeModule::parseCmdArgsObject_`. */
constexpr size_t JsonCmdTimeBuf = 768;
/** @brief JSON capacity for command args parsing in `PoolDeviceModule::parseCmdArgsObject_`. */
constexpr size_t JsonCmdPoolDeviceBuf = 256;
/** @brief JSON capacity for `ConfigStore::applyJson` root document. Kept aligned with cfg patch size limits. */
constexpr size_t JsonConfigApplyBuf = JsonCfgBuf;
/** @brief Maximum number of registered config variables in `ConfigStore` metadata table.
 *  Sized for current FlowIO/Supervisor profiles with additional headroom for new config branches
 *  (dashboard/LCD/PoolLogic extensions) while staying within DRAM budget. */
constexpr size_t MaxConfigVars = 380;
/** @brief Maximum NVS key length (without null terminator) enforced by `ConfigTypes::NVS_KEY`. */
constexpr size_t MaxNvsKeyLen = 15;
/** @brief FreeRTOS log queue length used by `LogHub` (`LogHubModule::init`).
 *  Increased moderately to absorb boot-time log bursts before log dispatcher task starts. */
constexpr uint8_t LogQueueLen = 64;
/** @brief FreeRTOS event queue length used by `EventBus` (`EventBus::QUEUE_LENGTH`).
 *  Sized to absorb startup bursts while limiting DRAM usage. */
constexpr uint8_t EventQueueLen = 40;
/** @brief Maximum number of EventBus subscribers (`EventBus::MAX_SUBSCRIBERS`). */
constexpr uint8_t EventSubscribersMax = 50;

/** @brief Core runtime limits shared by the module framework. */
namespace Core {
namespace Capacity {
/** @brief Maximum number of modules registered in `ModuleManager`. */
constexpr size_t MaxModules = 25;
/** @brief Maximum number of declared tasks tracked by `ModuleManager`. */
constexpr size_t MaxModuleTasks = 32;
}  // namespace Capacity

namespace Task {
/** @brief Default FreeRTOS task stack size used by `Module::taskStackSize`. */
constexpr uint16_t DefaultStackSize = 3072;
}  // namespace Task

namespace Timing {
/** @brief Small cooperative delay inserted after each `Module::loop` execution. */
constexpr uint32_t LoopDelayMs = 10;
}  // namespace Timing
}  // namespace Core

/** @brief Configuration tree limits shared by local and remote explorers. */
namespace Config {
namespace Capacity {
/** @brief Maximum number of unique config branches returned by `ConfigStore::listModules`. */
constexpr uint8_t ModuleListMax = 96;
}  // namespace Capacity
}  // namespace Config

/** @brief MQTT-specific limits grouped by concern to keep `SystemLimits` readable. */
namespace Mqtt {

#ifndef FLOW_MQTT_TASK_STACK_SIZE
#define FLOW_MQTT_TASK_STACK_SIZE 5712
#endif

/** @brief MQTT module task stack size returned by `MQTTModule::taskStackSize`. */
constexpr uint16_t TaskStackSize = FLOW_MQTT_TASK_STACK_SIZE;

/** @brief MQTT static capacities (queues, tables). */
namespace Capacity {
#ifndef FLOW_MQTT_RX_QUEUE_LEN
#define FLOW_MQTT_RX_QUEUE_LEN 8
#endif
#ifndef FLOW_MQTT_MAX_PUBLISHERS
#define FLOW_MQTT_MAX_PUBLISHERS 8
#endif
#ifndef FLOW_MQTT_CFG_TOPIC_MAX
#define FLOW_MQTT_CFG_TOPIC_MAX 48
#endif
#ifndef FLOW_MQTT_MAX_PRODUCERS
#define FLOW_MQTT_MAX_PRODUCERS 24
#endif
#ifndef FLOW_MQTT_MAX_INBOUND_HANDLERS
#define FLOW_MQTT_MAX_INBOUND_HANDLERS 16
#endif
#ifndef FLOW_MQTT_MAX_ACK_MESSAGES
#define FLOW_MQTT_MAX_ACK_MESSAGES 2
#endif
#ifndef FLOW_MQTT_MAX_JOBS
#define FLOW_MQTT_MAX_JOBS 80
#endif
#ifndef FLOW_MQTT_HIGH_QUEUE_CAP
#define FLOW_MQTT_HIGH_QUEUE_CAP 80
#endif
#ifndef FLOW_MQTT_NORMAL_QUEUE_CAP
#define FLOW_MQTT_NORMAL_QUEUE_CAP 80
#endif
#ifndef FLOW_MQTT_LOW_QUEUE_CAP
#define FLOW_MQTT_LOW_QUEUE_CAP 60
#endif

/** @brief FreeRTOS RX queue length for inbound MQTT messages in `MQTTModule`. */
constexpr uint8_t RxQueueLen = FLOW_MQTT_RX_QUEUE_LEN;
/** @brief Maximum number of runtime publishers stored in `MQTTModule::publishers`. */
constexpr uint8_t MaxPublishers = FLOW_MQTT_MAX_PUBLISHERS;
/** @brief Maximum number of `cfg/<module>` blocks tracked by `MQTTModule::cfgModules/topicCfgBlocks`. */
constexpr uint8_t CfgTopicMax = FLOW_MQTT_CFG_TOPIC_MAX;
/** @brief Maximum registered publish producers. */
constexpr uint8_t MaxProducers = FLOW_MQTT_MAX_PRODUCERS;
/** @brief Maximum registered inbound topic handlers. */
constexpr uint8_t MaxInboundHandlers = FLOW_MQTT_MAX_INBOUND_HANDLERS;
/** @brief Number of retained transient ACK payload slots. */
constexpr uint8_t MaxAckMessages = FLOW_MQTT_MAX_ACK_MESSAGES;
/** @brief Maximum active publish jobs. */
constexpr uint8_t MaxJobs = FLOW_MQTT_MAX_JOBS;
/** @brief High-priority publish queue capacity. */
constexpr uint16_t HighQueueCap = FLOW_MQTT_HIGH_QUEUE_CAP;
/** @brief Normal-priority publish queue capacity. */
constexpr uint16_t NormalQueueCap = FLOW_MQTT_NORMAL_QUEUE_CAP;
/** @brief Low-priority publish queue capacity. */
constexpr uint16_t LowQueueCap = FLOW_MQTT_LOW_QUEUE_CAP;

static_assert(RxQueueLen > 0, "FLOW_MQTT_RX_QUEUE_LEN must be at least 1");
static_assert(MaxPublishers > 0, "FLOW_MQTT_MAX_PUBLISHERS must be at least 1");
static_assert(MaxProducers >= 4, "FLOW_MQTT_MAX_PRODUCERS must keep built-in MQTT producers");
static_assert(MaxInboundHandlers > 0, "FLOW_MQTT_MAX_INBOUND_HANDLERS must be at least 1");
static_assert(MaxAckMessages > 0, "FLOW_MQTT_MAX_ACK_MESSAGES must be at least 1");
static_assert(MaxJobs > 0, "FLOW_MQTT_MAX_JOBS must be at least 1");
static_assert(HighQueueCap > 0 && NormalQueueCap > 0 && LowQueueCap > 0, "MQTT queues must be non-empty");
}  // namespace Capacity

/** @brief MQTT default configuration values. */
namespace Defaults {
/** @brief Default MQTT broker port used by `MQTTConfig::port` in `MQTTModule`. */
constexpr int32_t Port = 1883;
/** @brief Default minimum runtime publish period in ms for `mqtt.sensor_min_publish_ms`. */
constexpr uint32_t SensorMinPublishMs = 20000;
}  // namespace Defaults

/** @brief MQTT string/payload buffer sizes. */
namespace Buffers {
#ifndef FLOW_MQTT_BUF_HOST
#define FLOW_MQTT_BUF_HOST 64
#endif
#ifndef FLOW_MQTT_BUF_USER
#define FLOW_MQTT_BUF_USER 32
#endif
#ifndef FLOW_MQTT_BUF_PASS
#define FLOW_MQTT_BUF_PASS 32
#endif
#ifndef FLOW_MQTT_BUF_BASE_TOPIC
#define FLOW_MQTT_BUF_BASE_TOPIC 15
#endif
#ifndef FLOW_MQTT_BUF_DEVICE_ID
#define FLOW_MQTT_BUF_DEVICE_ID 15
#endif
#ifndef FLOW_MQTT_BUF_TOPIC
#define FLOW_MQTT_BUF_TOPIC 70
#endif
#ifndef FLOW_MQTT_BUF_DYNAMIC_TOPIC
#define FLOW_MQTT_BUF_DYNAMIC_TOPIC 160
#endif
#ifndef FLOW_MQTT_BUF_RX_TOPIC
#define FLOW_MQTT_BUF_RX_TOPIC 128
#endif
#ifndef FLOW_MQTT_BUF_RX_PAYLOAD
#define FLOW_MQTT_BUF_RX_PAYLOAD 384
#endif
#ifndef FLOW_MQTT_BUF_ACK
#define FLOW_MQTT_BUF_ACK 1536
#endif
#ifndef FLOW_MQTT_BUF_REPLY
#define FLOW_MQTT_BUF_REPLY 1024
#endif
#ifndef FLOW_MQTT_BUF_STATE_CFG
#define FLOW_MQTT_BUF_STATE_CFG 1536
#endif
#ifndef FLOW_MQTT_BUF_PUBLISH
#define FLOW_MQTT_BUF_PUBLISH 1536
#endif
#ifndef FLOW_MQTT_BUF_CMD_NAME
#define FLOW_MQTT_BUF_CMD_NAME 64
#endif
#ifndef FLOW_MQTT_BUF_CMD_ARGS
#define FLOW_MQTT_BUF_CMD_ARGS 320
#endif
#ifndef FLOW_MQTT_BUF_CMD_MODULE
#define FLOW_MQTT_BUF_CMD_MODULE 32
#endif

/** @brief MQTT config buffer length for `MQTTConfig::host` in `MQTTModule`. */
constexpr size_t Host = FLOW_MQTT_BUF_HOST;
/** @brief MQTT config buffer length for `MQTTConfig::user` in `MQTTModule`. */
constexpr size_t User = FLOW_MQTT_BUF_USER;
/** @brief MQTT config buffer length for `MQTTConfig::pass` in `MQTTModule`. */
constexpr size_t Pass = FLOW_MQTT_BUF_PASS;
/** @brief MQTT config buffer length for `MQTTConfig::baseTopic` in `MQTTModule`. */
constexpr size_t BaseTopic = FLOW_MQTT_BUF_BASE_TOPIC;
/** @brief MQTT device identifier buffer length used by `MQTTModule::deviceId` (e.g. `ESP32-XXXXXX`). */
constexpr size_t DeviceId = FLOW_MQTT_BUF_DEVICE_ID;
/** @brief MQTT full topic buffer length used by `MQTTModule` fixed topics (`cmd`, `ack`, `status`, `cfg/*`). */
constexpr size_t Topic = FLOW_MQTT_BUF_TOPIC;
/** @brief MQTT temporary topic buffer length for dynamic subtopics in `MQTTModule` (`cfg/<module>`, scheduler slots). */
constexpr size_t DynamicTopic = FLOW_MQTT_BUF_DYNAMIC_TOPIC;
/** @brief RX command topic buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxTopic = FLOW_MQTT_BUF_RX_TOPIC;
/** @brief RX command payload buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxPayload = FLOW_MQTT_BUF_RX_PAYLOAD;
/** @brief ACK JSON buffer length used by command/config acknowledge payloads. */
constexpr size_t Ack = FLOW_MQTT_BUF_ACK;
/** @brief Command handler reply buffer length used by `MQTTModule` (`replyBuf`).
 *  Must accommodate larger structured replies (e.g. `alarms.list` snapshots). */
constexpr size_t Reply = FLOW_MQTT_BUF_REPLY;
/** @brief Config JSON serialization buffer length used by module/state exports. */
constexpr size_t StateCfg = FLOW_MQTT_BUF_STATE_CFG;
/** @brief Runtime publish payload buffer length used by `MQTTModule` shared scratch buffer. */
constexpr size_t Publish = FLOW_MQTT_BUF_PUBLISH;
/** @brief Parsed command name buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdName = FLOW_MQTT_BUF_CMD_NAME;
/** @brief Serialized command args JSON buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdArgs = FLOW_MQTT_BUF_CMD_ARGS;
/** @brief Command module token buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdModule = FLOW_MQTT_BUF_CMD_MODULE;

static_assert(Host >= 16, "FLOW_MQTT_BUF_HOST is too small");
static_assert(BaseTopic >= 8, "FLOW_MQTT_BUF_BASE_TOPIC is too small");
static_assert(DeviceId >= 12, "FLOW_MQTT_BUF_DEVICE_ID must fit ESP32-XXXXXX");
static_assert(Topic >= 70, "FLOW_MQTT_BUF_TOPIC must fit existing Micronova topics");
static_assert(RxTopic >= Topic, "FLOW_MQTT_BUF_RX_TOPIC must fit full subscribed topics");
static_assert(RxPayload >= 32, "FLOW_MQTT_BUF_RX_PAYLOAD is too small");
static_assert(Ack >= 128 && Reply >= 128 && Publish >= 128, "MQTT payload buffers are too small");
static_assert(CmdName >= 16 && CmdArgs >= 64 && CmdModule >= 16, "MQTT command buffers are too small");
}  // namespace Buffers

/** @brief MQTT timing constants (runtime behavior). */
namespace Timing {
/** @brief Delay in ms between each retained `cfg/<module>` publish during startup ramp in `MQTTModule`. */
constexpr uint32_t CfgRampStepMs = 300;
/** @brief Startup retry window in ms for forced actuator runtime publishes in `MQTTModule`. */
constexpr uint32_t StartupActuatorRetryMs = 3000;
/** @brief Delay in ms while MQTT is disabled in `MQTTModule::loop`. */
constexpr uint32_t DisabledDelayMs = 2000;
/** @brief Network warmup delay in ms before first MQTT connect attempt in `MQTTModule::loop`. */
constexpr uint32_t NetWarmupMs = 2000;
/** @brief MQTT connection timeout in ms before forcing reconnect in `MQTTModule::loop`. */
constexpr uint32_t ConnectTimeoutMs = 10000;
/** @brief Main MQTT task loop delay in ms (`MQTTModule::loop`). */
constexpr uint32_t LoopDelayMs = 50;
}  // namespace Timing

/** @brief MQTT reconnect backoff profile. */
namespace Backoff {
/** @brief Minimum MQTT reconnect backoff in ms (`MQTTModule` error-wait state). */
constexpr uint32_t MinMs = 2000;
/** @brief MQTT reconnect backoff step #1 threshold in ms. */
constexpr uint32_t Step1Ms = 5000;
/** @brief MQTT reconnect backoff step #2 threshold in ms. */
constexpr uint32_t Step2Ms = 10000;
/** @brief MQTT reconnect backoff step #3 threshold in ms. */
constexpr uint32_t Step3Ms = 30000;
/** @brief MQTT reconnect backoff step #4 threshold in ms. */
constexpr uint32_t Step4Ms = 60000;
/** @brief Maximum MQTT reconnect backoff in ms. */
constexpr uint32_t MaxMs = 300000;
/** @brief Random jitter percentage applied to MQTT reconnect backoff delay. */
constexpr uint8_t JitterPct = 15;
}  // namespace Backoff

}  // namespace Mqtt
/** @brief Maximum number of runtime MQTT routes stored in the runtime mux (`main.cpp`). */
constexpr uint8_t MaxRuntimeRoutes = 36;
/** @brief Default momentary digital output pulse duration in ms (`IOModule`). */
constexpr uint16_t MomentaryPulseMs = 500;
/** @brief Default periodic trace interval for ORP/pH/PSI calc logs (`IOModule`, `trace_period_ms`). */
constexpr uint32_t IoTracePeriodMs = 10000;
/** @brief HA command payload buffer length for IO output switches (`IOModule::haSwitchPayloadOn_/Off_`). */
constexpr size_t IoHaSwitchPayloadBuf = 128;

/** @brief IO module compile-time capacities. Override per firmware env when a board needs less than FlowIO. */
namespace Io {
#ifndef FLOW_IO_MAX_ANALOG_ENDPOINTS
#define FLOW_IO_MAX_ANALOG_ENDPOINTS 17
#endif
#ifndef FLOW_IO_MAX_DIGITAL_INPUTS
#define FLOW_IO_MAX_DIGITAL_INPUTS 5
#endif
#ifndef FLOW_IO_MAX_DIGITAL_OUTPUTS
#define FLOW_IO_MAX_DIGITAL_OUTPUTS 10
#endif
#ifndef FLOW_IO_ANALOG_CFG_SLOTS
#define FLOW_IO_ANALOG_CFG_SLOTS FLOW_IO_MAX_ANALOG_ENDPOINTS
#endif
#ifndef FLOW_IO_DIGITAL_INPUT_CFG_SLOTS
#define FLOW_IO_DIGITAL_INPUT_CFG_SLOTS 5
#endif
#ifndef FLOW_IO_DIGITAL_OUTPUT_CFG_SLOTS
#define FLOW_IO_DIGITAL_OUTPUT_CFG_SLOTS FLOW_IO_MAX_DIGITAL_OUTPUTS
#endif

constexpr uint8_t MaxAnalogEndpoints = FLOW_IO_MAX_ANALOG_ENDPOINTS;
constexpr uint8_t MaxDigitalInputs = FLOW_IO_MAX_DIGITAL_INPUTS;
constexpr uint8_t MaxDigitalOutputs = FLOW_IO_MAX_DIGITAL_OUTPUTS;
constexpr uint8_t AnalogConfigSlots = FLOW_IO_ANALOG_CFG_SLOTS;
constexpr uint8_t DigitalInputConfigSlots = FLOW_IO_DIGITAL_INPUT_CFG_SLOTS;
constexpr uint8_t DigitalOutputConfigSlots = FLOW_IO_DIGITAL_OUTPUT_CFG_SLOTS;

static_assert(MaxAnalogEndpoints > 0, "FLOW_IO_MAX_ANALOG_ENDPOINTS must be at least 1");
static_assert(MaxDigitalInputs > 0, "FLOW_IO_MAX_DIGITAL_INPUTS must be at least 1");
static_assert(MaxDigitalOutputs > 0, "FLOW_IO_MAX_DIGITAL_OUTPUTS must be at least 1");
static_assert(AnalogConfigSlots >= 6, "FLOW_IO_ANALOG_CFG_SLOTS must keep legacy a00..a05 config vars");
static_assert(DigitalInputConfigSlots >= 5, "FLOW_IO_DIGITAL_INPUT_CFG_SLOTS must keep legacy i00..i04 config vars");
static_assert(DigitalOutputConfigSlots >= 8, "FLOW_IO_DIGITAL_OUTPUT_CFG_SLOTS must keep legacy d00..d07 config vars");
static_assert(AnalogConfigSlots >= MaxAnalogEndpoints, "analog config slots must cover runtime analog endpoints");
static_assert(DigitalInputConfigSlots >= MaxDigitalInputs, "digital input config slots must cover runtime inputs");
static_assert(DigitalOutputConfigSlots >= MaxDigitalOutputs, "digital output config slots must cover runtime outputs");
}  // namespace Io

/** @brief Alarm engine compile-time capacities and defaults. */
namespace Alarm {
/** @brief Maximum number of alarm slots managed by `AlarmModule`. */
constexpr uint16_t MaxAlarms = 16;
/** @brief Maximum JSON buffer used for alarm snapshot serialization. */
constexpr size_t SnapshotJsonBuf = 1536;
/** @brief Default alarm evaluation period in ms (`AlarmModule::loop`). */
constexpr uint32_t DefaultEvalPeriodMs = 250;
/** @brief JSON capacity for alarm command args parsing. */
constexpr size_t JsonCmdBuf = 256;
}  // namespace Alarm

/** @brief Shared WiFi runtime timings and buffers. */
namespace Wifi {
namespace Buffers {
/** @brief JSON document/output buffer used for WiFi scan status snapshots. */
constexpr size_t ScanStatusJson = 3072;
}  // namespace Buffers

namespace Timing {
/** @brief Minimum spacing between scan attempts when throttling WiFi scans. */
constexpr uint32_t ScanThrottleMs = 8000U;
/** @brief Initial delay before the first STA connect attempt after boot/config load. */
constexpr uint32_t InitialConnectDelayMs = 1200U;
/** @brief Startup window during which disconnect logs stay at debug level. */
constexpr uint32_t StartupTransientLogWindowMs = 10000U;
/** @brief Minimum spacing between repeated empty-SSID warnings. */
constexpr uint32_t EmptySsidLogIntervalMs = 10000U;
/** @brief Loop delay while WiFi runtime is disabled. */
constexpr uint32_t DisabledLoopDelayMs = 2000U;
/** @brief Loop delay while retries are disabled and WiFi is idle. */
constexpr uint32_t IdleRetryDisabledLoopDelayMs = 500U;
/** @brief Poll delay while waiting for the initial connect deadline. */
constexpr uint32_t IdleConnectPollDelayMs = 200U;
/** @brief Delay after issuing a STA connect attempt. */
constexpr uint32_t IdlePostConnectDelayMs = 1000U;
/** @brief Log cadence while the STA connect attempt is in progress. */
constexpr uint32_t ConnectingLogIntervalMs = 3000U;
/** @brief Delay before forcing a reconnect kick during connection attempts. */
constexpr uint32_t ReconnectKickDelayMs = 4000U;
/** @brief Timeout for a WiFi STA connect attempt before entering error wait. */
constexpr uint32_t ConnectTimeoutMs = 15000U;
/** @brief Loop delay while waiting for a WiFi STA connection. */
constexpr uint32_t ConnectingLoopDelayMs = 200U;
/** @brief Loop delay while WiFi remains connected. */
constexpr uint32_t ConnectedLoopDelayMs = 1000U;
/** @brief Error wait duration before retrying a failed WiFi connection. */
constexpr uint32_t ErrorWaitDurationMs = 5000U;
/** @brief Loop delay while staying in WiFi error-wait state. */
constexpr uint32_t ErrorWaitLoopDelayMs = 500U;
}  // namespace Timing
}  // namespace Wifi

/** @brief Home Assistant auto-discovery publication pacing limits. */
namespace Ha {
namespace Timing {
/** @brief Delay in ms between each HA discovery entity publish in `HAModule`. */
constexpr uint32_t DiscoveryStepMs = 200;
}  // namespace Timing
}  // namespace Ha

/** @brief Shared heap guards before network publishes (MQTT and HA discovery). */
namespace NetworkPublish {
/** @brief Minimum free 8-bit heap (bytes) required before attempting publish. */
constexpr uint32_t MinFreeHeapBytes = 4000U;
/** @brief Minimum largest 8-bit free block (bytes) required before attempting publish. */
constexpr uint32_t MinLargestBlockBytes = 4096U;
}  // namespace NetworkPublish

/** @brief Boot orchestration timings used in `main.cpp` staged startup. */
namespace Boot {
/** @brief Delay in ms before allowing MQTT connection attempts (`MQTTModule::setStartupReady`). */
#if defined(FLOW_PROFILE_MICRONOVA)
constexpr uint32_t WifiProvisioningStartDelayMs = 4000;
constexpr uint32_t IoStartDelayMs = 8000;
constexpr uint32_t MicronovaBusStartDelayMs = 12000;
constexpr uint32_t MicronovaBoilerStartDelayMs = 16000;
constexpr uint32_t MqttStartDelayMs = 22000;
constexpr uint32_t WebInterfaceStartDelayMs = 34000;
#else
constexpr uint32_t WifiProvisioningStartDelayMs = 0;
constexpr uint32_t IoStartDelayMs = 0;
constexpr uint32_t MicronovaBusStartDelayMs = 0;
constexpr uint32_t MicronovaBoilerStartDelayMs = 0;
constexpr uint32_t MqttStartDelayMs = 1500;
constexpr uint32_t WebInterfaceStartDelayMs = 10000;
#endif
/** @brief Delay in ms before enabling HA auto-discovery publishing (`HAModule::setStartupReady`). */
constexpr uint32_t HaStartDelayMs = 15000;
/** @brief Delay in ms before enabling PoolLogic control loop (`PoolLogicModule::setStartupReady`). */
constexpr uint32_t PoolLogicStartDelayMs = 10000;
}  // namespace Boot

/** @brief Firmware update networking limits shared by the update module. */
namespace FirmwareUpdate {
namespace Http {
/** @brief HTTP connect timeout used for firmware and cfgdocs downloads. */
constexpr uint16_t ConnectTimeoutMs = 15000U;
/** @brief HTTP request timeout used once the download has started. */
constexpr uint32_t RequestTimeoutMs = 60000U;
/** @brief Timeout while waiting for the next chunk from a download stream. */
constexpr uint32_t StreamReadTimeoutMs = 15000U;
/** @brief Scratch buffer size used to copy download streams chunk by chunk. */
constexpr size_t StreamChunkBytes = 1024U;
}  // namespace Http
}  // namespace FirmwareUpdate

}  // namespace Limits
