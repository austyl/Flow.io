#pragma once
/**
 * @file DisplayUdpClientModule.h
 * @brief Remote Display firmware UDP bridge between FlowIO and a local Nextion.
 */

#include <WiFiUdp.h>

#include "Core/Hmi/HmiUdpProtocol.h"
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"

class DisplayUdpClientModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::DisplayUdpClient; }
    const char* taskName() const override { return "displayudp"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 4096; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }
    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

    bool begin();
    void tick(uint32_t nowMs);

private:
    static constexpr uint32_t HelloPeriodMs = 1000U;
    static constexpr uint32_t PingPeriodMs = 2000U;
    static constexpr uint32_t LinkTimeoutMs = 9000U;
    static constexpr uint32_t AckRetryMs = 150U;
    static constexpr uint8_t AckMaxAttempts = 3U;

    struct ConfigData {
        char token[33]{};
    } cfgData_{};

    // CFGDOC: {"label":"Token Display UDP", "help":"Token partage optionnel envoye en CRC dans Hello pour l'appairage FlowIO."}
    ConfigVariable<char,0> tokenVar_{
        NVS_KEY(NvsKeys::Hmi::RemoteUdpToken), "token", "hmi/remote_udp",
        ConfigType::CharArray, cfgData_.token, ConfigPersistence::Persistent, sizeof(cfgData_.token)
    };

    WiFiUDP udp_{};
    NextionDriver nextion_{};
    const WifiService* wifiSvc_ = nullptr;

    uint8_t rxBuf_[HMI_UDP_MAX_PACKET]{};
    uint8_t txBuf_[HMI_UDP_MAX_PACKET]{};

    IPAddress flowIp_{};
    uint16_t flowPort_ = HMI_UDP_PORT;
    bool started_ = false;
    bool linked_ = false;
    bool lostShown_ = false;

    uint16_t txSeq_ = 1;
    uint16_t lastAck_ = 0;
    uint16_t lastRxSeq_ = 0;

    uint32_t lastHelloMs_ = 0;
    uint32_t lastPingMs_ = 0;
    uint32_t lastSeenMs_ = 0;

    uint8_t pendingBuf_[HMI_UDP_MAX_PACKET]{};
    size_t pendingLen_ = 0;
    uint16_t pendingSeq_ = 0;
    uint8_t pendingAttempts_ = 0;
    uint32_t pendingLastSendMs_ = 0;

    ConfigMenuView configView_{};

    void sendHello_(uint32_t nowMs);
    void sendPing_(uint32_t nowMs);
    void readUdp_(uint32_t nowMs);
    void handlePacket_(const HmiUdpHeader& header, const uint8_t* payload, uint32_t nowMs);
    bool sendPacket_(HmiUdpMsgType type, const void* payload, uint8_t payloadLen, uint8_t flags = 0);
    bool sendAck_(uint16_t seq);
    void pollNextion_();
    bool sendEvent_(const HmiEvent& event);
    void servicePendingAck_(uint32_t nowMs);
    void markSeen_(uint32_t nowMs);
    void handleLinkLost_();
    bool wifiConnected_() const;
    static void copyText_(char* out, size_t outLen, const char* in);
};
