/**
 * @file St7789SupervisorDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Modules/SupervisorHMIModule/Drivers/FlowIoLogoBitmap.h"

namespace {
constexpr uint16_t rgb565_(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static constexpr uint16_t kColorBg = rgb565_(16, 22, 34);
static constexpr uint16_t kColorHeader = rgb565_(18, 24, 36);
static constexpr uint16_t kColorText = rgb565_(255, 255, 255);
static constexpr uint16_t kColorMuted = rgb565_(205, 214, 224);
static constexpr uint16_t kColorDivider = rgb565_(86, 94, 108);
static constexpr uint16_t kColorWifiOff = rgb565_(88, 96, 110);
static constexpr uint16_t kColorOn = rgb565_(56, 214, 106);
static constexpr uint16_t kColorOff = rgb565_(110, 118, 128);
static constexpr uint16_t kColorAlarmAct = rgb565_(224, 72, 72);
static constexpr uint16_t kColorAlarmAck = rgb565_(240, 178, 85);
static constexpr uint16_t kColorGaugeOk = rgb565_(47, 158, 104);
static constexpr uint16_t kColorGaugeCardBg = rgb565_(22, 30, 44);
static constexpr uint16_t kColorGaugeTrack = rgb565_(55, 66, 84);

static constexpr int16_t kHeaderH = 58;
static constexpr int16_t kSidePad = 8;
static constexpr int16_t kStatusPillW = 54;
static constexpr int16_t kStatusPillH = 20;
static constexpr int16_t kAlarmPillW = 54;
static constexpr int16_t kAlarmPillH = 20;
static constexpr int16_t kAlarmSummaryH = 28;
static constexpr uint8_t kMaxWifiBars = 5;
static constexpr uint8_t kRowCount = 7;
static constexpr uint32_t kAlarmPageRotateMs = 10000U;

enum class SupervisorPage : uint8_t {
    Status = 0,
    Measures = 1,
    Alarms = 2,
};

struct AlarmRowDef {
    const char* label;
};

struct GaugeBand {
    float from;
    float to;
    uint16_t color;
};

static constexpr AlarmRowDef kAlarmRows[kSupervisorAlarmSlotCount] = {
    {"Pression basse"},
    {"Pression haute"},
    {"Niv. bidon pH"},
    {"Niv. bidon chlore"},
};

static constexpr GaugeBand kPhGaugeBands[] = {
    {6.4f, 6.8f, kColorAlarmAct},
    {6.8f, 7.0f, kColorAlarmAck},
    {7.0f, 7.6f, kColorGaugeOk},
    {7.6f, 7.8f, kColorAlarmAck},
    {7.8f, 8.4f, kColorAlarmAct},
};

static constexpr GaugeBand kOrpGaugeBands[] = {
    {350.0f, 500.0f, kColorAlarmAct},
    {500.0f, 620.0f, kColorAlarmAck},
    {620.0f, 760.0f, kColorGaugeOk},
    {760.0f, 820.0f, kColorAlarmAck},
    {820.0f, 900.0f, kColorAlarmAct},
};

static constexpr GaugeBand kWaterTempGaugeBands[] = {
    {0.0f, 8.0f, kColorAlarmAct},
    {8.0f, 14.0f, kColorAlarmAck},
    {14.0f, 30.0f, kColorGaugeOk},
    {30.0f, 34.0f, kColorAlarmAck},
    {34.0f, 40.0f, kColorAlarmAct},
};

static constexpr GaugeBand kAirTempGaugeBands[] = {
    {-10.0f, 0.0f, kColorAlarmAct},
    {0.0f, 8.0f, kColorAlarmAck},
    {8.0f, 28.0f, kColorGaugeOk},
    {28.0f, 35.0f, kColorAlarmAck},
    {35.0f, 45.0f, kColorAlarmAct},
};

uint16_t panelColor_(bool swapBytes, uint16_t c)
{
    if (!swapBytes) return c;
    return (uint16_t)((c << 8) | (c >> 8));
}

int clampi_(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float clampf_(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint8_t pctFromRssi_(int32_t dbm)
{
    const int v = clampi_((int)dbm, -100, -40);
    return (uint8_t)(((v + 100) * 100) / 60);
}

uint8_t barsFromPct_(uint8_t pct)
{
    return (uint8_t)((pct + 19U) / 20U);
}

int16_t textWidth_(const char* txt, uint8_t textSize)
{
    if (!txt) return 0;
    return (int16_t)(strlen(txt) * 6U * textSize);
}

void setDefaultFont_(SupervisorSt7789& d, bool swapBytes, uint16_t fg, uint16_t bg, uint8_t size)
{
    d.setFont(nullptr);
    d.setTextSize(size);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void setGfxFont_(SupervisorSt7789& d, bool swapBytes, const GFXfont* font, uint16_t fg, uint16_t bg)
{
    d.setFont(font);
    d.setTextSize(1);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void textBounds_(SupervisorSt7789& d,
                 const char* txt,
                 int16_t x,
                 int16_t y,
                 int16_t& x1,
                 int16_t& y1,
                 uint16_t& w,
                 uint16_t& h)
{
    if (!txt) txt = "";
    d.getTextBounds(txt, x, y, &x1, &y1, &w, &h);
}

int16_t gfxTextWidth_(SupervisorSt7789& d, const char* txt)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)w;
}

int16_t gfxBaselineCenteredInBox_(SupervisorSt7789& d, const char* txt, int16_t boxY, int16_t boxH)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)(boxY + ((boxH - (int16_t)h) / 2) - y1);
}

void drawGfxText_(SupervisorSt7789& d,
                  bool swapBytes,
                  const GFXfont* font,
                  uint16_t fg,
                  uint16_t bg,
                  int16_t x,
                  int16_t baselineY,
                  const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, baselineY);
    d.print(txt ? txt : "");
}

void drawGfxTextCenteredY_(SupervisorSt7789& d,
                           bool swapBytes,
                           const GFXfont* font,
                           uint16_t fg,
                           uint16_t bg,
                           int16_t x,
                           int16_t boxY,
                           int16_t boxH,
                           const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, gfxBaselineCenteredInBox_(d, txt, boxY, boxH));
    d.print(txt ? txt : "");
}

void drawDashedHLine_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, int16_t w, uint16_t color)
{
    for (int16_t dx = 0; dx < w; dx += 6) {
        d.drawFastHLine((int16_t)(x + dx), y, 4, panelColor_(swapBytes, color));
    }
}

void drawWifiBars_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint8_t bars)
{
    static constexpr int16_t kBarW = 6;
    static constexpr int16_t kStep = 9;
    static constexpr int16_t kUnitH = 4;

    for (uint8_t i = 1; i <= kMaxWifiBars; ++i) {
        const int16_t h = (int16_t)(i * kUnitH);
        const int16_t yi = (int16_t)(y + (kMaxWifiBars * kUnitH) - h);
        const uint16_t color = (i <= bars) ? kColorText : kColorWifiOff;
        d.fillRoundRect((int16_t)(x + (int16_t)((i - 1) * kStep)),
                        yi,
                        kBarW,
                        h,
                        1,
                        panelColor_(swapBytes, color));
    }
}

void normalizeAlarmLabel_(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && (j + 1) < outLen; ++i) {
        const char c = in[i];
        out[j++] = (c == '_' || c == '-') ? ' ' : c;
    }
    out[j] = '\0';
}

void drawStatusPill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, bool on)
{
    const uint16_t fill = on ? kColorOn : kColorOff;
    const char* label = on ? "ON" : "OFF";

    d.fillRoundRect(x, y, kStatusPillW, kStatusPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kStatusPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kStatusPillH));
    d.print(label);
}

void drawHeaderPill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, int16_t w, int16_t h, const char* label, bool on)
{
    const uint16_t fill = on ? kColorOn : rgb565_(224, 72, 72);
    d.fillRoundRect(x, y, w, h, 5, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSans9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((w - tw) / 2)), (int16_t)(gfxBaselineCenteredInBox_(d, label, y, h) + 3));
    d.print(label);
}

void drawBootLogo_(SupervisorSt7789& d, bool swapBytes)
{
    const int16_t w = d.width();
    const int16_t h = d.height();
    d.fillScreen(panelColor_(swapBytes, rgb565_(255, 255, 255)));
    const int16_t x = (int16_t)((w - (int16_t)kFlowIoLogoWidth) / 2);
    const int16_t y = (int16_t)((h - (int16_t)kFlowIoLogoHeight) / 2);
    d.drawRGBBitmap(x, y, kFlowIoLogoBitmap, kFlowIoLogoWidth, kFlowIoLogoHeight);
}

void drawStaticLayout_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, SupervisorPage page)
{
    d.fillScreen(panelColor_(swapBytes, kColorBg));
    d.fillRect(0, 0, w, kHeaderH, panelColor_(swapBytes, kColorHeader));
    d.drawFastHLine(0, (int16_t)(kHeaderH - 1), w, panelColor_(swapBytes, kColorDivider));

    const int16_t bodyTop = kHeaderH + 4;
    if (page == SupervisorPage::Status) {
        const int16_t rowH = (int16_t)((h - bodyTop) / kRowCount);
        for (uint8_t i = 0; i + 1U < kRowCount; ++i) {
            const int16_t y = (int16_t)(bodyTop + ((int16_t)(i + 1U) * rowH) - 1);
            drawDashedHLine_(d, swapBytes, kSidePad, y, (int16_t)(w - (2 * kSidePad)), kColorDivider);
        }
        return;
    }

    if (page == SupervisorPage::Measures) {
        return;
    }

    d.drawFastHLine(0, (int16_t)(bodyTop + kAlarmSummaryH - 1), w, panelColor_(swapBytes, kColorDivider));
    const int16_t rowH = (int16_t)((h - bodyTop - kAlarmSummaryH) / (int16_t)kSupervisorAlarmSlotCount);
    for (uint8_t i = 0; i + 1U < kSupervisorAlarmSlotCount; ++i) {
        const int16_t y = (int16_t)(bodyTop + kAlarmSummaryH + ((int16_t)(i + 1U) * rowH) - 1);
        drawDashedHLine_(d, swapBytes, kSidePad, y, (int16_t)(w - (2 * kSidePad)), kColorDivider);
    }
}

void drawHeaderWifi_(SupervisorSt7789& d, bool swapBytes, bool hasRssi, int32_t rssiDbm)
{
    const int16_t w = d.width();
    const int16_t areaW = 58;
    const int16_t x = (int16_t)(w - areaW);
    d.fillRect(x, 0, areaW, 26, panelColor_(swapBytes, kColorHeader));
    const uint8_t wifiBars = hasRssi ? barsFromPct_(pctFromRssi_(rssiDbm)) : 0U;
    drawWifiBars_(d, swapBytes, (int16_t)(w - 50), 6, wifiBars);
}

void drawHeaderTime_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* timeTxt)
{
    (void)w;
    static constexpr int16_t kTimeAreaW = 118;
    d.fillRect(0, 0, kTimeAreaW, 32, panelColor_(swapBytes, kColorHeader));
    setGfxFont_(d, swapBytes, &FreeSans18pt7b, kColorText, kColorHeader);
    d.setCursor(8, 26);
    d.print(timeTxt ? timeTxt : "--:--");
}

void drawHeaderMqtt_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* timeTxt, bool mqttReady)
{
    (void)timeTxt;
    const int16_t pillW = 82;
    const int16_t pillH = 26;
    const int16_t pillX = (int16_t)((w - pillW) / 2);
    const int16_t clearX = (int16_t)(pillX - 10);
    const int16_t clearW = (int16_t)(pillW + 20);
    d.fillRect(clearX, 0, clearW, 32, panelColor_(swapBytes, kColorHeader));
    drawHeaderPill_(d, swapBytes, pillX, -6, pillW, pillH, "MQTT", mqttReady);
}

void drawHeaderDateIp_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* dateTxt, const char* ip)
{
    d.fillRect(0, 28, w, (int16_t)(kHeaderH - 29), panelColor_(swapBytes, kColorHeader));
    drawGfxText_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorHeader, 8, 50, dateTxt ? dateTxt : "--/--/----");
    if (ip && ip[0]) {
        setGfxFont_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorHeader);
        const int16_t ipX = (int16_t)(w - gfxTextWidth_(d, ip) - 8);
        d.setCursor(ipX, 50);
        d.print(ip);
    }
}

void drawRow_(SupervisorSt7789& d, bool swapBytes, int16_t w, uint8_t rowIndex, const char* label, bool on)
{
    const int16_t bodyTop = kHeaderH + 4;
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / kRowCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kStatusPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kStatusPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawStatusPill_(d, swapBytes, pillX, pillY, on);
}

const char* alarmStateLabel_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return "ACT";
        case SupervisorAlarmState::Acked: return "ACK";
        case SupervisorAlarmState::Clear:
        default:
            return "CLR";
    }
}

uint16_t alarmStateColor_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return kColorAlarmAct;
        case SupervisorAlarmState::Acked: return kColorAlarmAck;
        case SupervisorAlarmState::Clear:
        default:
            return kColorOff;
    }
}

void drawAlarmStatePill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, SupervisorAlarmState state)
{
    const uint16_t fill = alarmStateColor_(state);
    const char* label = alarmStateLabel_(state);

    d.fillRoundRect(x, y, kAlarmPillW, kAlarmPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kAlarmPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kAlarmPillH));
    d.print(label);
}

void drawAlarmSummary_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t w,
                       uint8_t actCount,
                       uint8_t ackCount,
                       uint8_t clrCount)
{
    const int16_t bodyTop = kHeaderH + 4;
    char summary[48] = {0};
    snprintf(summary, sizeof(summary), "Alarmes: %u ACT   %u ACK   %u CLR",
             (unsigned)actCount,
             (unsigned)ackCount,
             (unsigned)clrCount);

    d.fillRect(0, bodyTop, w, (int16_t)(kAlarmSummaryH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSansBold9pt7b, kColorText, kColorBg, kSidePad, bodyTop, kAlarmSummaryH, summary);
}

void drawAlarmRow_(SupervisorSt7789& d,
                   bool swapBytes,
                   int16_t w,
                   uint8_t rowIndex,
                   const char* label,
                   SupervisorAlarmState state)
{
    const int16_t bodyTop = (int16_t)(kHeaderH + 4 + kAlarmSummaryH);
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / (int16_t)kSupervisorAlarmSlotCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kAlarmPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kAlarmPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawAlarmStatePill_(d, swapBytes, pillX, pillY, state);
}

void drawAlarmBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, const SupervisorHmiViewModel& vm)
{
    drawAlarmSummary_(d, swapBytes, w, vm.flowAlarmActCount, vm.flowAlarmAckCount, vm.flowAlarmClrCount);
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        drawAlarmRow_(d, swapBytes, w, i, kAlarmRows[i].label, vm.flowAlarmStates[i]);
    }
}

float gaugeValueToAngle_(float value, float minValue, float maxValue)
{
    if (!(maxValue > minValue)) return -100.0f;
    const float ratio = (clampf_(value, minValue, maxValue) - minValue) / (maxValue - minValue);
    return -100.0f + (ratio * 200.0f);
}

void polarPoint_(int16_t cx, int16_t cy, float radius, float angleDeg, int16_t& x, int16_t& y)
{
    const float radians = angleDeg * 0.01745329252f;
    x = (int16_t)lroundf((float)cx + (cosf(radians) * radius));
    y = (int16_t)lroundf((float)cy + (sinf(radians) * radius));
}

void drawArcStroke_(SupervisorSt7789& d,
                    bool swapBytes,
                    int16_t cx,
                    int16_t cy,
                    int16_t radius,
                    float startDeg,
                    float endDeg,
                    uint16_t color,
                    int16_t thickness)
{
    if (!(endDeg > startDeg)) return;
    const int16_t half = (int16_t)(thickness / 2);
    for (int16_t offset = -half; offset <= half; ++offset) {
        const int16_t rr = (int16_t)(radius + offset);
        int16_t prevX = 0;
        int16_t prevY = 0;
        bool havePrev = false;
        for (float angle = startDeg; angle <= endDeg; angle += 4.0f) {
            int16_t x = 0;
            int16_t y = 0;
            polarPoint_(cx, cy, (float)rr, angle, x, y);
            if (havePrev) {
                d.drawLine(prevX, prevY, x, y, panelColor_(swapBytes, color));
            }
            prevX = x;
            prevY = y;
            havePrev = true;
        }
        int16_t endX = 0;
        int16_t endY = 0;
        polarPoint_(cx, cy, (float)rr, endDeg, endX, endY);
        d.drawLine(prevX, prevY, endX, endY, panelColor_(swapBytes, color));
    }
}

uint16_t resolveGaugeColor_(float value, bool hasValue, const GaugeBand* bands, size_t bandCount)
{
    if (!hasValue || !bands || bandCount == 0U) return kColorMuted;
    const float clamped = clampf_(value, bands[0].from, bands[bandCount - 1U].to);
    for (size_t i = 0; i < bandCount; ++i) {
        if (clamped >= bands[i].from && clamped <= bands[i].to) return bands[i].color;
    }
    return bands[bandCount - 1U].color;
}

void drawGaugeMarker_(SupervisorSt7789& d,
                      bool swapBytes,
                      int16_t cx,
                      int16_t cy,
                      int16_t radius,
                      float angleDeg,
                      uint16_t color)
{
    int16_t tipX = 0;
    int16_t tipY = 0;
    int16_t baseLX = 0;
    int16_t baseLY = 0;
    int16_t baseRX = 0;
    int16_t baseRY = 0;
    polarPoint_(cx, cy, (float)(radius + 4), angleDeg, tipX, tipY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg - 5.5f, baseLX, baseLY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg + 5.5f, baseRX, baseRY);
    d.fillTriangle(tipX, tipY, baseLX, baseLY, baseRX, baseRY, panelColor_(swapBytes, color));
}

void formatGaugeValue_(char* out, size_t outLen, bool hasValue, float value, uint8_t decimals, const char* unit)
{
    if (!out || outLen == 0U) return;
    if (!hasValue) {
        snprintf(out, outLen, "--");
        return;
    }
    char numberBuf[24] = {0};
    if (decimals > 0U) {
        snprintf(numberBuf, sizeof(numberBuf), "%.*f", (int)decimals, (double)value);
    } else {
        snprintf(numberBuf, sizeof(numberBuf), "%ld", lroundf(value));
    }
    if (unit && unit[0] != '\0') {
        snprintf(out, outLen, "%s %s", numberBuf, unit);
        return;
    }
    snprintf(out, outLen, "%s", numberBuf);
}

void drawMeasureGauge_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       const char* label,
                       bool hasValue,
                       float value,
                       float minValue,
                       float maxValue,
                       uint8_t decimals,
                       const char* unit,
                       const GaugeBand* bands,
                       size_t bandCount)
{
    (void)label;
    d.fillRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorDivider));
    const uint16_t valueColor = resolveGaugeColor_(value, hasValue, bands, bandCount);

    char valueBuf[24] = {0};
    formatGaugeValue_(valueBuf, sizeof(valueBuf), hasValue, value, decimals, nullptr);
    setGfxFont_(d, swapBytes, &FreeSans24pt7b, hasValue ? valueColor : kColorMuted, kColorGaugeCardBg);
    const int16_t valueX = (int16_t)(x + ((w - gfxTextWidth_(d, valueBuf)) / 2));
    const int16_t valueBoxY = y;
    const int16_t valueBoxH = (unit && unit[0] != '\0') ? (int16_t)(h - 22) : h;
    d.setCursor(valueX, gfxBaselineCenteredInBox_(d, valueBuf, valueBoxY, valueBoxH));
    d.print(valueBuf);

    if (unit && unit[0] != '\0') {
        setGfxFont_(d, swapBytes, &FreeSansBold12pt7b, hasValue ? valueColor : kColorMuted, kColorGaugeCardBg);
        const int16_t unitX = (int16_t)(x + ((w - gfxTextWidth_(d, unit)) / 2));
        d.setCursor(unitX, (int16_t)(y + h - 14));
        d.print(unit);
    }
}

void drawMeasuresBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, const SupervisorHmiViewModel& vm)
{
    const int16_t bodyTop = (int16_t)(kHeaderH + 8);
    const int16_t bodyBottomPad = 8;
    const int16_t gap = 8;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    const int16_t cardH = (int16_t)((h - bodyTop - bodyBottomPad - gap) / 2);
    const int16_t xLeft = kSidePad;
    const int16_t xRight = (int16_t)(kSidePad + cardW + gap);
    const int16_t yTop = bodyTop;
    const int16_t yBottom = (int16_t)(bodyTop + cardH + gap);

    drawMeasureGauge_(d,
                      swapBytes,
                      xLeft,
                      yTop,
                      cardW,
                      cardH,
                      "pH",
                      vm.flowHasPh,
                      vm.flowPhValue,
                      6.4f,
                      8.4f,
                      2,
                      "",
                      kPhGaugeBands,
                      sizeof(kPhGaugeBands) / sizeof(kPhGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xRight,
                      yTop,
                      cardW,
                      cardH,
                      "ORP",
                      vm.flowHasOrp,
                      vm.flowOrpValue,
                      350.0f,
                      900.0f,
                      0,
                      "mV",
                      kOrpGaugeBands,
                      sizeof(kOrpGaugeBands) / sizeof(kOrpGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xLeft,
                      yBottom,
                      cardW,
                      cardH,
                      "Eau",
                      vm.flowHasWaterTemp,
                      vm.flowWaterTemp,
                      0.0f,
                      40.0f,
                      1,
                      "C",
                      kWaterTempGaugeBands,
                      sizeof(kWaterTempGaugeBands) / sizeof(kWaterTempGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xRight,
                      yBottom,
                      cardW,
                      cardH,
                      "Air",
                      vm.flowHasAirTemp,
                      vm.flowAirTemp,
                      -10.0f,
                      45.0f,
                      1,
                      "C",
                      kAirTempGaugeBands,
                      sizeof(kAirTempGaugeBands) / sizeof(kAirTempGaugeBands[0]));
}
}

St7789SupervisorDriver::St7789SupervisorDriver(const St7789SupervisorDriverConfig& cfg)
    : cfg_(cfg),
      display_(cfg.csPin, cfg.dcPin, cfg.rstPin)
{
}

bool St7789SupervisorDriver::begin()
{
    if (started_) return true;
    const bool swapBytes = cfg_.swapColorBytes;
    SPI.begin(cfg_.sclkPin, -1, cfg_.mosiPin, cfg_.csPin);
    display_.setSPISpeed(cfg_.spiHz);
    display_.init(cfg_.resX, cfg_.resY);
    display_.setColRowStart(cfg_.colStart, cfg_.rowStart);
    display_.setRotation(cfg_.rotation & 0x03U);
    display_.invertDisplay(cfg_.invertColors);
    display_.fillScreen(panelColor_(swapBytes, kColorBg));
    display_.setTextWrap(false);
    display_.setTextSize(1);

    if (cfg_.backlightPin >= 0) {
        pinMode(cfg_.backlightPin, OUTPUT);
        digitalWrite(cfg_.backlightPin, HIGH);
        backlightOn_ = true;
    }

    drawBootLogo_(display_, swapBytes);
    delay(450);

    started_ = true;
    layoutDrawn_ = false;
    lastRenderMs_ = 0;
    lastTime_[0] = '\0';
    lastDate_[0] = '\0';
    lastIp_[0] = '\0';
    lastHasRssi_ = false;
    lastRssiDbm_ = -127;
    lastMqttReady_ = false;
    memset(lastRows_, 0, sizeof(lastRows_));
    lastHasPh_ = false;
    lastPhValue_ = 0.0f;
    lastHasOrp_ = false;
    lastOrpValue_ = 0.0f;
    lastHasWaterTemp_ = false;
    lastWaterTemp_ = 0.0f;
    lastHasAirTemp_ = false;
    lastAirTemp_ = 0.0f;
    lastPage_ = 0xFFU;
    lastAlarmActCount_ = 0xFFU;
    lastAlarmAckCount_ = 0xFFU;
    lastAlarmClrCount_ = 0xFFU;
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        lastAlarmStates_[i] = SupervisorAlarmState::Clear;
    }
    return true;
}

void St7789SupervisorDriver::setBacklight(bool on)
{
    if (cfg_.backlightPin < 0) return;
    if (backlightOn_ == on) return;
    digitalWrite(cfg_.backlightPin, on ? HIGH : LOW);
    backlightOn_ = on;
}

const char* St7789SupervisorDriver::wifiStateText_(WifiState st) const
{
    switch (st) {
        case WifiState::Disabled: return "disabled";
        case WifiState::Idle: return "idle";
        case WifiState::Connecting: return "connecting";
        case WifiState::Connected: return "connected";
        case WifiState::ErrorWait: return "retry_wait";
        default: return "?";
    }
}

const char* St7789SupervisorDriver::netModeText_(NetworkAccessMode mode) const
{
    switch (mode) {
        case NetworkAccessMode::Station: return "sta";
        case NetworkAccessMode::AccessPoint: return "ap";
        case NetworkAccessMode::None:
        default:
            return "none";
    }
}

bool St7789SupervisorDriver::render(const SupervisorHmiViewModel& vm, bool force)
{
    if (!started_) return false;
    const uint32_t now = millis();
    if (!force && cfg_.minRenderGapMs > 0U && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return true;
    }

    const int16_t w = display_.width();
    const int16_t h = display_.height();
    const bool swapBytes = cfg_.swapColorBytes;
    const int32_t activeRssi = vm.flowHasRssi ? vm.flowRssiDbm : vm.rssiDbm;
    const bool hasAnyRssi = vm.flowHasRssi || vm.hasRssi;
    const SupervisorPage page = static_cast<SupervisorPage>((now / kAlarmPageRotateMs) % 2U);

    char timeBuf[16] = "--:--";
    char dateBuf[20] = "--/--/----";
    time_t t = time(nullptr);
    if (t > 1600000000) {
        struct tm tmv{};
        localtime_r(&t, &tmv);
        (void)strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmv);
        (void)strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &tmv);
    }

    struct RowItem {
        const char* label;
        bool on;
    };

    const RowItem rows[] = {
        {"Filtration auto", vm.flowFiltrationAuto},
        {"Regulation pH auto", vm.flowPhAutoMode},
        {"Regulation ORP auto", vm.flowOrpAutoMode},
        {"Hivernage", vm.flowWinterMode},
        {"Pompe", vm.flowFiltrationOn},
        {"Pompe pH", vm.flowPhPumpOn},
        {"Pompe Chlore", vm.flowChlorinePumpOn},
    };

    const bool pageChanged = (lastPage_ != (uint8_t)page);
    if (!layoutDrawn_ || force || pageChanged) {
        drawStaticLayout_(display_, swapBytes, w, h, page);
        layoutDrawn_ = true;
        lastPage_ = (uint8_t)page;
        lastTime_[0] = '\0';
        lastDate_[0] = '\0';
        lastIp_[0] = '\0';
        lastHasRssi_ = !hasAnyRssi;
        lastRssiDbm_ = hasAnyRssi ? (activeRssi - 1) : -126;
        lastMqttReady_ = !vm.flowMqttReady;
        lastHasPh_ = !vm.flowHasPh;
        lastPhValue_ = vm.flowPhValue - 1.0f;
        lastHasOrp_ = !vm.flowHasOrp;
        lastOrpValue_ = vm.flowOrpValue - 1.0f;
        lastHasWaterTemp_ = !vm.flowHasWaterTemp;
        lastWaterTemp_ = vm.flowWaterTemp - 1.0f;
        lastHasAirTemp_ = !vm.flowHasAirTemp;
        lastAirTemp_ = vm.flowAirTemp - 1.0f;
        lastAlarmActCount_ = 0xFFU;
        lastAlarmAckCount_ = 0xFFU;
        lastAlarmClrCount_ = 0xFFU;
    }

    if (lastHasRssi_ != hasAnyRssi || lastRssiDbm_ != activeRssi) {
        drawHeaderWifi_(display_, swapBytes, hasAnyRssi, activeRssi);
        lastHasRssi_ = hasAnyRssi;
        lastRssiDbm_ = activeRssi;
    }

    if (strcmp(lastTime_, timeBuf) != 0) {
        drawHeaderTime_(display_, swapBytes, w, timeBuf);
        snprintf(lastTime_, sizeof(lastTime_), "%s", timeBuf);
    }

    if (strcmp(lastTime_, timeBuf) == 0 && lastMqttReady_ != vm.flowMqttReady) {
        drawHeaderMqtt_(display_, swapBytes, w, timeBuf, vm.flowMqttReady);
        lastMqttReady_ = vm.flowMqttReady;
    } else if (lastMqttReady_ != vm.flowMqttReady) {
        drawHeaderMqtt_(display_, swapBytes, w, timeBuf, vm.flowMqttReady);
        lastMqttReady_ = vm.flowMqttReady;
    }

    if (strcmp(lastDate_, dateBuf) != 0 || strcmp(lastIp_, vm.ip) != 0) {
        drawHeaderDateIp_(display_, swapBytes, w, dateBuf, vm.ip);
        snprintf(lastDate_, sizeof(lastDate_), "%s", dateBuf);
        snprintf(lastIp_, sizeof(lastIp_), "%s", vm.ip);
    }

    if (page == SupervisorPage::Status) {
        for (uint8_t i = 0; i < kRowCount; ++i) {
            if (force || pageChanged || lastRows_[i] != rows[i].on) {
                drawRow_(display_, swapBytes, w, i, rows[i].label, rows[i].on);
                lastRows_[i] = rows[i].on;
            }
        }
    } else if (page == SupervisorPage::Measures) {
        const bool measuresChanged = force || pageChanged ||
                                     (lastHasPh_ != vm.flowHasPh) ||
                                     (fabsf(lastPhValue_ - vm.flowPhValue) > 0.005f) ||
                                     (lastHasOrp_ != vm.flowHasOrp) ||
                                     (fabsf(lastOrpValue_ - vm.flowOrpValue) > 0.5f) ||
                                     (lastHasWaterTemp_ != vm.flowHasWaterTemp) ||
                                     (fabsf(lastWaterTemp_ - vm.flowWaterTemp) > 0.05f) ||
                                     (lastHasAirTemp_ != vm.flowHasAirTemp) ||
                                     (fabsf(lastAirTemp_ - vm.flowAirTemp) > 0.05f);
        if (measuresChanged) {
            drawMeasuresBody_(display_, swapBytes, w, h, vm);
            lastHasPh_ = vm.flowHasPh;
            lastPhValue_ = vm.flowPhValue;
            lastHasOrp_ = vm.flowHasOrp;
            lastOrpValue_ = vm.flowOrpValue;
            lastHasWaterTemp_ = vm.flowHasWaterTemp;
            lastWaterTemp_ = vm.flowWaterTemp;
            lastHasAirTemp_ = vm.flowHasAirTemp;
            lastAirTemp_ = vm.flowAirTemp;
        }
    } else {
        bool alarmBodyChanged = force || pageChanged ||
                                lastAlarmActCount_ != vm.flowAlarmActCount ||
                                lastAlarmAckCount_ != vm.flowAlarmAckCount ||
                                lastAlarmClrCount_ != vm.flowAlarmClrCount;
        for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
            if (lastAlarmStates_[i] != vm.flowAlarmStates[i]) {
                alarmBodyChanged = true;
            }
        }
        if (alarmBodyChanged) {
            drawAlarmBody_(display_, swapBytes, w, vm);
            lastAlarmActCount_ = vm.flowAlarmActCount;
            lastAlarmAckCount_ = vm.flowAlarmAckCount;
            lastAlarmClrCount_ = vm.flowAlarmClrCount;
            for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
                lastAlarmStates_[i] = vm.flowAlarmStates[i];
            }
        }
    }

    setDefaultFont_(display_, swapBytes, kColorText, kColorBg, 1);

    layoutDrawn_ = true;
    lastRenderMs_ = now;
    return true;
}
