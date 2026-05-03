#pragma once

#include <stdint.h>

enum class MicronovaPowerState : uint8_t {
    Unknown = 0,
    Off,
    On,
    Alarm
};

static inline const char* micronovaPowerStateText(MicronovaPowerState state)
{
    switch (state) {
        case MicronovaPowerState::Off: return "OFF";
        case MicronovaPowerState::On: return "ON";
        case MicronovaPowerState::Alarm: return "ALARM";
        case MicronovaPowerState::Unknown:
        default:
            return "UNKNOWN";
    }
}

static inline const char* micronovaStoveStateText(uint8_t code)
{
    switch (code) {
        case 0: return "Éteint";
        case 1: return "Démarrage";
        case 2: return "Chargement granulés";
        case 3: return "Allumage";
        case 4: return "Allumé";
        case 5: return "Nettoyage brasier";
        case 6: return "Nettoyage final";
        case 7: return "Veille";
        case 8: return "Granulés manquants";
        case 9: return "Échec allumage";
        case 10: return "Alarme";
        default: return "Inconnu";
    }
}

static inline MicronovaPowerState micronovaPowerStateFromStoveState(uint8_t code)
{
    if (code == 0 || code == 6 || code == 7 || code == 9) return MicronovaPowerState::Off;
    if (code >= 1 && code <= 5) return MicronovaPowerState::On;
    if (code == 8 || code == 10) return MicronovaPowerState::Alarm;
    return MicronovaPowerState::Unknown;
}
