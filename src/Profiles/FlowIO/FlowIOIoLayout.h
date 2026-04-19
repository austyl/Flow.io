#pragma once

#include "Board/FlowIODINBoards.h"
#include "Domain/DomainTypes.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IOModuleTypes.h"

namespace Profiles {
namespace FlowIO {
namespace IoLayout {

enum : PhysicalPortId {
    PortAdsInternal0 = 100, // ADS1115 interne, entree single-ended A0.
    PortAdsInternal1 = 101, // ADS1115 interne, entree single-ended A1.
    PortAdsInternal2 = 102, // ADS1115 interne, entree single-ended A2.
    PortAdsInternal3 = 103, // ADS1115 interne, entree single-ended A3.
    PortAdsExternal0 = 110, // ADS1115 externe, paire differentielle 0.
    PortAdsExternal1 = 111, // ADS1115 externe, paire differentielle 1.
    PortDsWater = 120, // DS18B20: sonde eau.
    PortDsAir = 121, // DS18B20: sonde air.
    PortSht40Temp = 130, // SHT40: temperature.
    PortSht40Humidity = 131, // SHT40: humidite.
    PortBmp280Temp = 132, // BMP280: temperature.
    PortBmp280Pressure = 133, // BMP280: pression.
    PortBme680Temp = 134, // BME680: temperature.
    PortBme680Humidity = 135, // BME680: humidite.
    PortBme680Pressure = 136, // BME680: pression.
    PortBme680Gas = 137, // BME680: resistance gaz.
    PortIna226ShuntMv = 138, // INA226: tension shunt (mV).
    PortIna226BusV = 139, // INA226: tension bus (V).
    PortIna226CurrentMa = 140, // INA226: courant (mA).
    PortIna226PowerMw = 141, // INA226: puissance (mW).
    PortIna226LoadV = 142, // INA226: tension charge (V).
    PortDigitalIn1 = 200, // Entree digitale 1 (GPIO board).
    PortDigitalIn2 = 201, // Entree digitale 2 (GPIO board).
    PortDigitalIn3 = 202, // Entree digitale 3 (GPIO board).
    PortDigitalIn4 = 203, // Entree digitale 4 (GPIO board).
    PortRelay1 = 300, // Sortie relais 1.
    PortRelay2 = 301, // Sortie relais 2.
    PortRelay3 = 302, // Sortie relais 3.
    PortRelay4 = 303, // Sortie relais 4 (momentary).
    PortRelay5 = 304, // Sortie relais 5.
    PortRelay6 = 305, // Sortie relais 6.
    PortRelay7 = 306, // Sortie relais 7.
    PortRelay8 = 307, // Sortie relais 8.
    PortPcf0Bit0 = 400, // PCF8574 #0, bit 0.
    PortPcf0Bit1 = 401, // PCF8574 #0, bit 1.
    PortPcf0Bit2 = 402, // PCF8574 #0, bit 2.
    PortPcf0Bit3 = 403, // PCF8574 #0, bit 3.
    PortPcf0Bit4 = 404, // PCF8574 #0, bit 4.
    PortPcf0Bit5 = 405, // PCF8574 #0, bit 5.
    PortPcf0Bit6 = 406, // PCF8574 #0, bit 6.
    PortPcf0Bit7 = 407 // PCF8574 #0, bit 7.
};

inline constexpr IOBindingPortSpec kBindingPorts[] = {
    // {portId, kind, param0, param1}
    {PortAdsInternal0, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 0, 0}, // ADS1115 interne canal 0.
    {PortAdsInternal1, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 1, 0}, // ADS1115 interne canal 1.
    {PortAdsInternal2, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 2, 0}, // ADS1115 interne canal 2.
    {PortAdsInternal3, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 3, 0}, // ADS1115 interne canal 3.
    {PortAdsExternal0, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 0, 0}, // ADS1115 externe paire 0.
    {PortAdsExternal1, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 1, 0}, // ADS1115 externe paire 1.
    {PortDsWater, IO_PORT_KIND_DS18_WATER, 0, 0}, // DS18B20 eau.
    {PortDsAir, IO_PORT_KIND_DS18_AIR, 0, 0}, // DS18B20 air.
    {PortSht40Temp, IO_PORT_KIND_SHT40, 0, 0}, // SHT40 temperature.
    {PortSht40Humidity, IO_PORT_KIND_SHT40, 1, 0}, // SHT40 humidite.
    {PortBmp280Temp, IO_PORT_KIND_BMP280, 0, 0}, // BMP280 temperature.
    {PortBmp280Pressure, IO_PORT_KIND_BMP280, 1, 0}, // BMP280 pression.
    {PortBme680Temp, IO_PORT_KIND_BME680, 0, 0}, // BME680 temperature.
    {PortBme680Humidity, IO_PORT_KIND_BME680, 1, 0}, // BME680 humidite.
    {PortBme680Pressure, IO_PORT_KIND_BME680, 2, 0}, // BME680 pression.
    {PortBme680Gas, IO_PORT_KIND_BME680, 3, 0}, // BME680 gaz.
    {PortIna226ShuntMv, IO_PORT_KIND_INA226, 0, 0}, // INA226 shunt.
    {PortIna226BusV, IO_PORT_KIND_INA226, 1, 0}, // INA226 bus.
    {PortIna226CurrentMa, IO_PORT_KIND_INA226, 2, 0}, // INA226 courant.
    {PortIna226PowerMw, IO_PORT_KIND_INA226, 3, 0}, // INA226 puissance.
    {PortIna226LoadV, IO_PORT_KIND_INA226, 4, 0}, // INA226 tension charge.
    {PortDigitalIn1, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIODINv1IoPoints[8].pin, 0}, // Entree digitale 1 via GPIO de la board.
    {PortDigitalIn2, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIODINv1IoPoints[9].pin, 0}, // Entree digitale 2 via GPIO de la board.
    {PortDigitalIn3, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIODINv1IoPoints[10].pin, 0}, // Entree digitale 3 via GPIO de la board.
    {PortDigitalIn4, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIODINv1IoPoints[11].pin, 0}, // Entree digitale 4 via GPIO de la board.
    {PortRelay1, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[0].pin, 0}, // Relais 1 via GPIO de la board.
    {PortRelay2, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[1].pin, 0}, // Relais 2 via GPIO de la board.
    {PortRelay3, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[2].pin, 0}, // Relais 3 via GPIO de la board.
    {PortRelay4, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[3].pin, 0}, // Relais 4 via GPIO de la board.
    {PortRelay5, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[4].pin, 0}, // Relais 5 via GPIO de la board.
    {PortRelay6, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[5].pin, 0}, // Relais 6 via GPIO de la board.
    {PortRelay7, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[6].pin, 0}, // Relais 7 via GPIO de la board.
    {PortRelay8, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIODINv1IoPoints[7].pin, 0}, // Relais 8 via GPIO de la board.
    {PortPcf0Bit0, IO_PORT_KIND_PCF8574_OUTPUT, 0, 0}, // PCF8574 bit 0.
    {PortPcf0Bit1, IO_PORT_KIND_PCF8574_OUTPUT, 1, 0}, // PCF8574 bit 1.
    {PortPcf0Bit2, IO_PORT_KIND_PCF8574_OUTPUT, 2, 0}, // PCF8574 bit 2.
    {PortPcf0Bit3, IO_PORT_KIND_PCF8574_OUTPUT, 3, 0}, // PCF8574 bit 3.
    {PortPcf0Bit4, IO_PORT_KIND_PCF8574_OUTPUT, 4, 0}, // PCF8574 bit 4.
    {PortPcf0Bit5, IO_PORT_KIND_PCF8574_OUTPUT, 5, 0}, // PCF8574 bit 5.
    {PortPcf0Bit6, IO_PORT_KIND_PCF8574_OUTPUT, 6, 0}, // PCF8574 bit 6.
    {PortPcf0Bit7, IO_PORT_KIND_PCF8574_OUTPUT, 7, 0}, // PCF8574 bit 7.
};

constexpr PhysicalPortId analogPortFromLegacy(uint8_t source, uint8_t channel)
{
    switch (source) {
        case IO_SRC_ADS_INTERNAL_SINGLE:
            return (channel == 0U) ? PortAdsInternal0 :
                   (channel == 1U) ? PortAdsInternal1 :
                   (channel == 2U) ? PortAdsInternal2 :
                                     PortAdsInternal3;
        case IO_SRC_ADS_EXTERNAL_DIFF:
            return (channel == 0U) ? PortAdsExternal0 : PortAdsExternal1;
        case IO_SRC_DS18_WATER:
            return PortDsWater;
        case IO_SRC_DS18_AIR:
            return PortDsAir;
        default:
            return IO_PORT_INVALID;
    }
}

struct AnalogRoleDefault {
    DomainRole role; // Role fonctionnel de la sonde.
    PhysicalPortId bindingPort; // Port physique associe.
    float c0; // Coefficient de calibration offset/intercept.
    float c1; // Coefficient de calibration gain/slope.
    int32_t precision; // Precision d'affichage (nb de decimales).
};

inline constexpr AnalogRoleDefault kAnalogRoleDefaults[] = {
    // {role, bindingPort, c0, c1, precision}
    {DomainRole::OrpSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A0S, FLOW_WIRDEF_IO_A0C), FLOW_WIRDEF_IO_A00, FLOW_WIRDEF_IO_A01, FLOW_WIRDEF_IO_A0P}, // ORP.
    {DomainRole::PhSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A1S, FLOW_WIRDEF_IO_A1C), FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P}, // pH.
    {DomainRole::PsiSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A2S, FLOW_WIRDEF_IO_A2C), FLOW_WIRDEF_IO_A20, FLOW_WIRDEF_IO_A21, FLOW_WIRDEF_IO_A2P}, // Pression.
    {DomainRole::SpareAnalog, analogPortFromLegacy(FLOW_WIRDEF_IO_A3S, FLOW_WIRDEF_IO_A3C), FLOW_WIRDEF_IO_A30, FLOW_WIRDEF_IO_A31, FLOW_WIRDEF_IO_A3P}, // Entree analogique reservee.
    {DomainRole::WaterTemp, analogPortFromLegacy(FLOW_WIRDEF_IO_A4S, FLOW_WIRDEF_IO_A4C), FLOW_WIRDEF_IO_A40, FLOW_WIRDEF_IO_A41, FLOW_WIRDEF_IO_A4P}, // Temperature eau.
    {DomainRole::AirTemp, analogPortFromLegacy(FLOW_WIRDEF_IO_A5S, FLOW_WIRDEF_IO_A5C), FLOW_WIRDEF_IO_A50, FLOW_WIRDEF_IO_A51, FLOW_WIRDEF_IO_A5P}, // Temperature air.
};

struct DigitalInputRoleDefault {
    DomainRole role; // Role fonctionnel de l'entree.
    PhysicalPortId bindingPort; // Port physique associe.
    uint8_t mode; // Mode de lecture (etat/counter).
    uint8_t edgeMode; // Type de front pris en compte.
    uint32_t debounceUs; // Debounce en microsecondes.
};

inline constexpr DigitalInputRoleDefault kDigitalInputRoleDefaults[] = {
    // {role, bindingPort, mode, edgeMode, debounceUs}
    {DomainRole::PoolLevelSensor, PortDigitalIn1, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau piscine.
    {DomainRole::PhLevelSensor, PortDigitalIn2, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau pH.
    {DomainRole::ChlorineLevelSensor, PortDigitalIn3, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U}, // Capteur niveau chlore.
    {DomainRole::WaterCounterSensor, PortDigitalIn4, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 100000U}, // Compteur impulsions eau (100 ms debounce).
};

struct DigitalOutputRoleDefault {
    DomainRole role; // Role fonctionnel de la sortie.
    PhysicalPortId bindingPort; // Port physique associe.
    bool activeHigh; // Polarite de commande logique.
    bool momentary; // True si sortie impulsionnelle.
    uint16_t pulseMs; // Duree d'impulsion en ms.
};

inline constexpr DigitalOutputRoleDefault kDigitalOutputRoleDefaults[] = {
    // {role, bindingPort, activeHigh, momentary, pulseMs}
    {DomainRole::FiltrationPump, PortRelay1, false, false, 0U}, // Pompe filtration.
    {DomainRole::PhPump, PortRelay2, false, false, 0U}, // Pompe pH.
    {DomainRole::ChlorinePump, PortRelay3, false, false, 0U}, // Pompe chlore.
    {DomainRole::ChlorineGenerator, PortRelay4, false, BoardProfiles::kFlowIODINv1IoPoints[3].momentary, BoardProfiles::kFlowIODINv1IoPoints[3].pulseMs}, // Electrolyseur; utilise limites board (momentary/pulseMs).
    {DomainRole::Robot, PortRelay5, false, false, 0U}, // Robot.
    {DomainRole::Lights, PortRelay6, false, false, 0U}, // Eclairage.
    {DomainRole::FillPump, PortRelay7, false, false, 0U}, // Pompe de remplissage.
    {DomainRole::WaterHeater, PortRelay8, false, false, 0U}, // Chauffage.
};

inline constexpr const AnalogRoleDefault* analogDefaultForRole(DomainRole role)
{
    for (const AnalogRoleDefault& entry : kAnalogRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalInputRoleDefault* digitalInputDefaultForRole(DomainRole role)
{
    for (const DigitalInputRoleDefault& entry : kDigitalInputRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalOutputRoleDefault* digitalOutputDefaultForRole(DomainRole role)
{
    for (const DigitalOutputRoleDefault& entry : kDigitalOutputRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

}  // namespace IoLayout
}  // namespace FlowIO
}  // namespace Profiles
