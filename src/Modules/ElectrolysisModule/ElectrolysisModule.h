#pragma once
/**
 * @file ElectrolysisModule.h
 * @brief I2C client for a dedicated electrolysis controller.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class ElectrolysisModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::Electrolysis; }
    const char* taskName() const override { return "electrolysis"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 2560; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 2; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Io;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    void loop() override;

private:
    struct ConfigData {
        bool enabled = false;
        uint8_t address = ElectrolysisProtocol::PreferredAddress;
        uint16_t pollPeriodMs = 500;
        uint16_t maxCurrentMa = 0;
    } cfgData_{};

    // CFGDOC: {"label":"Client electrolyse actif","help":"Active le client I2C vers le controleur d'electrolyse dedie."}
    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::Electrolysis::Enabled), "enabled", "electrolysis",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Adresse I2C electrolyse","help":"Adresse I2C du controleur d'electrolyse dedie."}
    ConfigVariable<uint8_t, 0> addressVar_{
        NVS_KEY(NvsKeys::Electrolysis::Address), "address", "electrolysis",
        ConfigType::UInt8, &cfgData_.address, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Periode polling electrolyse","help":"Periode d'echange I2C avec le controleur d'electrolyse.","unit":"ms"}
    ConfigVariable<uint16_t, 0> pollPeriodVar_{
        NVS_KEY(NvsKeys::Electrolysis::PollPeriodMs), "poll_ms", "electrolysis",
        ConfigType::UInt16, &cfgData_.pollPeriodMs, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Courant cellule maximum","help":"Courant maximum autorise pour la cellule. La valeur 0 laisse la limite au controleur dedie.","unit":"mA"}
    ConfigVariable<uint16_t, 0> maxCurrentVar_{
        NVS_KEY(NvsKeys::Electrolysis::MaxCurrentMa), "max_current_ma", "electrolysis",
        ConfigType::UInt16, &cfgData_.maxCurrentMa, ConfigPersistence::Persistent, 0
    };

    const IOServiceV2* ioSvc_ = nullptr;
    ElectrolysisService service_{
        &ElectrolysisModule::availableStatic_,
        &ElectrolysisModule::writeRequestStatic_,
        &ElectrolysisModule::readRuntimeStatic_,
        this
    };

    ElectrolysisRequest request_{};
    ElectrolysisRuntime runtime_{};
    portMUX_TYPE requestMux_ = portMUX_INITIALIZER_UNLOCKED;
    portMUX_TYPE runtimeMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t seq_ = 0;
    uint8_t heartbeat_ = 0;
    uint32_t lastPollMs_ = 0;
    uint32_t lastWarnMs_ = 0;

    static uint8_t availableStatic_(void* ctx);
    static ElectrolysisSvcStatus writeRequestStatic_(void* ctx, const ElectrolysisRequest* request);
    static ElectrolysisSvcStatus readRuntimeStatic_(void* ctx, ElectrolysisRuntime* outRuntime);

    uint8_t available_() const;
    ElectrolysisSvcStatus writeRequest_(const ElectrolysisRequest* request);
    ElectrolysisSvcStatus readRuntime_(ElectrolysisRuntime* outRuntime);

    void pollController_(uint32_t nowMs);
    void setOnline_(bool online, uint32_t nowMs);
    void updateRuntimeFromStatus_(const ElectrolysisProtocol::StatusFrame& status, uint32_t nowMs);
    ElectrolysisProtocol::CommandFrame buildCommandFrame_(const ElectrolysisRequest& request);
    void warnTransferFailure_(IoStatus st, uint32_t nowMs);
};
