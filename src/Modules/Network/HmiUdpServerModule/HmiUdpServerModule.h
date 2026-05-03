#pragma once
/**
 * @file HmiUdpServerModule.h
 * @brief Lightweight FlowIO UDP endpoint for a remote HMI display.
 */

#include <WiFiUdp.h>

#include "Core/Hmi/HmiUdpProtocol.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class HmiUdpServerModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::HmiUdpServer; }
    const char* taskName() const override { return "hmiudp"; }
    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override {}

    bool begin();
    void tick(uint32_t nowMs);

    bool isDisplayOnline() const { return displayOnline_; }
    bool consumeFullRefreshRequested();

    bool sendHomeText(HmiHomeTextField field, const char* text);
    bool sendHomeGauge(HmiHomeGaugeField field, uint16_t percent);
    bool sendHomeStateBits(uint32_t stateBits);
    bool sendHomeAlarmBits(uint32_t alarmBits);

    bool sendConfigStart(const ConfigMenuView& view);
    bool sendConfigRow(uint8_t row, const ConfigMenuRowView& viewRow, ConfigMenuMode mode);
    bool sendConfigEnd(uint8_t rowCount);

    bool sendRtcWrite(const HmiRtcDateTime& value);
    bool requestRtcRead();

    bool pollEvent(HmiEvent& out);

private:
    static constexpr uint8_t HMI_UDP_EVENT_QUEUE_SIZE = 4;
    static constexpr uint32_t OfflineTimeoutMs = 9000U;

    struct ConfigData {
        char token[33]{};
    } cfgData_{};

    // CFGDOC: {"label":"Token Display UDP", "help":"Token partage optionnel. Si renseigne, le Display doit envoyer le meme token sous forme CRC."}
    ConfigVariable<char,0> tokenVar_{
        NVS_KEY(NvsKeys::Hmi::RemoteUdpToken), "token", "hmi/remote_udp",
        ConfigType::CharArray, cfgData_.token, ConfigPersistence::Persistent, sizeof(cfgData_.token)
    };

    WiFiUDP udp_{};
    const WifiService* wifiSvc_ = nullptr;

    uint8_t rxBuf_[HMI_UDP_MAX_PACKET]{};
    uint8_t txBuf_[HMI_UDP_MAX_PACKET]{};

    IPAddress remoteIp_{};
    uint16_t remotePort_ = HMI_UDP_PORT;
    bool started_ = false;
    bool displayOnline_ = false;
    bool fullRefreshRequested_ = false;
    uint16_t txSeq_ = 1;
    uint16_t lastRxSeq_ = 0;
    uint16_t lastEventSeq_ = 0;
    bool hasLastEventSeq_ = false;
    uint32_t lastSeenMs_ = 0;

    HmiEvent eventQueue_[HMI_UDP_EVENT_QUEUE_SIZE]{};
    uint8_t eventHead_ = 0;
    uint8_t eventTail_ = 0;

    bool sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags = 0);
    bool sendAck_(uint16_t seq);
    void readUdp_(uint32_t nowMs);
    void handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs);
    void markRemote_(uint32_t nowMs);
    bool pushEvent_(const HmiEvent& event);
    bool wifiConnected_() const;
    bool tokenAccepted_(uint32_t tokenCrc) const;
    static void copyText_(char* out, size_t outLen, const char* in);
};
