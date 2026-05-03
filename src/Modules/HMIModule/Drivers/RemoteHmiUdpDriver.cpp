#include "Modules/HMIModule/Drivers/RemoteHmiUdpDriver.h"

bool RemoteHmiUdpDriver::begin()
{
    return udpServer_ && udpServer_->begin();
}

void RemoteHmiUdpDriver::tick(uint32_t nowMs)
{
    if (udpServer_) udpServer_->tick(nowMs);
}

bool RemoteHmiUdpDriver::pollEvent(HmiEvent& out)
{
    return udpServer_ && udpServer_->pollEvent(out);
}

bool RemoteHmiUdpDriver::publishHomeText(HmiHomeTextField field, const char* text)
{
    return udpServer_ && udpServer_->sendHomeText(field, text);
}

bool RemoteHmiUdpDriver::publishHomeGaugePercent(HmiHomeGaugeField field, uint16_t percent)
{
    return udpServer_ && udpServer_->sendHomeGauge(field, percent);
}

bool RemoteHmiUdpDriver::publishHomeStateBits(uint32_t stateBits)
{
    return udpServer_ && udpServer_->sendHomeStateBits(stateBits);
}

bool RemoteHmiUdpDriver::publishHomeAlarmBits(uint32_t alarmBits)
{
    return udpServer_ && udpServer_->sendHomeAlarmBits(alarmBits);
}

bool RemoteHmiUdpDriver::readRtc(HmiRtcDateTime& out, uint16_t timeoutMs)
{
    (void)timeoutMs;
    out = HmiRtcDateTime{};
    // TODO: add a small pending-response slot if FlowIO needs remote RTC reads.
    return udpServer_ && udpServer_->requestRtcRead() && false;
}

bool RemoteHmiUdpDriver::writeRtc(const HmiRtcDateTime& value)
{
    return udpServer_ && udpServer_->sendRtcWrite(value);
}

bool RemoteHmiUdpDriver::renderConfigMenu(const ConfigMenuView& view)
{
    if (!udpServer_) return false;
    bool ok = udpServer_->sendConfigStart(view);
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        ok = udpServer_->sendConfigRow(i, view.rows[i], view.mode) && ok;
    }
    ok = udpServer_->sendConfigEnd(view.rowCountOnPage) && ok;
    return ok;
}

bool RemoteHmiUdpDriver::refreshConfigMenuValues(const ConfigMenuView& view)
{
    if (!udpServer_) return false;
    bool ok = true;
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        if (!view.rows[i].visible || !view.rows[i].valueVisible) continue;
        ok = udpServer_->sendConfigRow(i, view.rows[i], view.mode) && ok;
    }
    return ok;
}
