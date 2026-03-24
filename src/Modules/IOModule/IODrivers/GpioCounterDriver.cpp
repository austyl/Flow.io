/**
 * @file GpioCounterDriver.cpp
 * @brief Implementation file.
 */

#include "GpioCounterDriver.h"

namespace {
portMUX_TYPE gGpioCounterMux = portMUX_INITIALIZER_UNLOCKED;
}

GpioCounterDriver::GpioCounterDriver(const char* driverId,
                                     uint8_t pin,
                                     bool activeHigh,
                                     uint8_t inputPullMode,
                                     uint32_t counterDebounceUs)
    : driverId_(driverId),
      pin_(pin),
      activeHigh_(activeHigh),
      inputPullMode_(inputPullMode),
      counterDebounceUs_(counterDebounceUs)
{
}

bool GpioCounterDriver::begin()
{
    if (inputPullMode_ == 1) pinMode(pin_, INPUT_PULLUP);
    else if (inputPullMode_ == 2) pinMode(pin_, INPUT_PULLDOWN);
    else pinMode(pin_, INPUT);

    int rawLevel = digitalRead(pin_);
    lastLogicalState_ = activeHigh_ ? (rawLevel == HIGH) : (rawLevel == LOW);
    pulseCount_ = 0;
    lastPulseUs_ = 0;
    attachInterruptArg(pin_, &GpioCounterDriver::handleInterruptThunk_, this, CHANGE);
    return true;
}

bool GpioCounterDriver::read(bool& on) const
{
    int level = digitalRead(pin_);
    on = activeHigh_ ? (level == HIGH) : (level == LOW);
    return true;
}

bool GpioCounterDriver::readCount(int32_t& count) const
{
    portENTER_CRITICAL(&gGpioCounterMux);
    count = pulseCount_;
    portEXIT_CRITICAL(&gGpioCounterMux);
    return true;
}

void IRAM_ATTR GpioCounterDriver::handleInterruptThunk_(void* arg)
{
    if (!arg) return;
    static_cast<GpioCounterDriver*>(arg)->handleInterrupt_();
}

void IRAM_ATTR GpioCounterDriver::handleInterrupt_()
{
    int level = digitalRead(pin_);
    const bool logicalOn = activeHigh_ ? (level == HIGH) : (level == LOW);

    if (!logicalOn) {
        lastLogicalState_ = false;
        return;
    }

    if (lastLogicalState_) return;

    const uint32_t nowUs = micros();
    if (counterDebounceUs_ > 0 && lastPulseUs_ != 0U) {
        if ((uint32_t)(nowUs - lastPulseUs_) < counterDebounceUs_) {
            lastLogicalState_ = true;
            return;
        }
    }

    portENTER_CRITICAL_ISR(&gGpioCounterMux);
    if (pulseCount_ < INT32_MAX) ++pulseCount_;
    lastPulseUs_ = nowUs;
    lastLogicalState_ = true;
    portEXIT_CRITICAL_ISR(&gGpioCounterMux);
}
