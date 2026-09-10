#ifndef WAKE_ON_WEIGHT_H
#define WAKE_ON_WEIGHT_H

#include <Arduino.h>
#include "config.h"
#include "parameter.h"
#ifdef ESP32
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#endif

#ifdef ADS1232ADC
#include <ADS1232_ADC.h>
#include "declare.h"

const float WOW_TRIGGER_GRAMS = 50.0f;
const unsigned long WOW_READ_TIMEOUT_MS = 900;
const unsigned long WOW_RAIL_SETTLE_MS = 100;
const int32_t WOW_MIN_THRESHOLD_RAW = 500;
const uint32_t WOW_RTC_MAGIC = 0x574F5733;
const uint8_t WOW_INTERVAL_COUNT = 4;
const uint64_t wowIntervalUs[WOW_INTERVAL_COUNT] = { 0, 2000000, 3000000, 4000000 };

void wowCaptureBaselineForSleep() {
  wowRtc.armed = 0;
  if (i_wow_interval <= 0 || i_wow_interval >= WOW_INTERVAL_COUNT || i_lowBatteryCount > 0) return;
  if (isfinite(f_batteryVoltage) && f_batteryVoltage > 0 && f_batteryVoltage < lowBatteryThreshold) return;
  if (f_calibration_value == CALIBRATION_VALUE_DEFAULT) return;
  if (!isValidCalibrationValue(f_calibration_value)) return;
  const float threshold = WOW_TRIGGER_GRAMS * fabsf(f_calibration_value);
  const auto info = scale.getDebugInfo();
  if (info.validSamples <= 0 || info.dataOutOfRange || info.signalTimeout) return;
  wowRtc.magic = WOW_RTC_MAGIC;
  wowRtc.armed = 1;
  wowRtc.baselineRaw = info.smoothedValue;
  wowRtc.thresholdRaw = max((int32_t)(threshold + 0.5f), WOW_MIN_THRESHOLD_RAW);
  wowRtc.intervalUs = wowIntervalUs[i_wow_interval];
}

void wowArmSleepTimer() {
  if (wowRtc.armed && wowRtc.intervalUs > 0) {
    esp_sleep_enable_timer_wakeup(wowRtc.intervalUs);
    wowTimerArmedThisBoot = true;
  } else if (wowTimerArmedThisBoot) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    wowTimerArmedThisBoot = false;
  }
}

static bool wowPhysicalWakeRequested() {
  const int pin = rtc_gpio_get_level((gpio_num_t)BUTTON_SQUARE) == LOW ? BUTTON_SQUARE
                  : rtc_gpio_get_level((gpio_num_t)BUTTON_CIRCLE) == LOW ? BUTTON_CIRCLE
                  : rtc_gpio_get_level((gpio_num_t)BATTERY_CHARGING) == LOW ? BATTERY_CHARGING
                  : -1;
  if (pin < 0) return false;
  GPIO_power_on_with = pin;
  wowButtonWake = pin != BATTERY_CHARGING;
  return true;
}

static bool wowWaitForRail() {
  const unsigned long startedAt = millis();
  while (millis() - startedAt < WOW_RAIL_SETTLE_MS) {
    if (wowPhysicalWakeRequested()) return false;
    delay(2);
  }
  return !wowPhysicalWakeRequested();
}

static bool wowReadWakeSamples(ADS1232_ADC &adc, int32_t &rawSample, bool &outOfRange) {
  const unsigned long startedAt = millis();
  uint8_t samplesRead = 0;
  int32_t previousSample = 0;
  while (millis() - startedAt < WOW_READ_TIMEOUT_MS) {
    if (wowPhysicalWakeRequested()) return false;
    if (digitalRead(SCALE_DOUT) == LOW) {
      if (adc.update()) {
        const auto info = adc.getDebugInfo();
        wowWakeDiagnostics.raw[samplesRead] = info.rawValue;
        samplesRead++;
        wowWakeDiagnostics.samplesRead = samplesRead;
        if (samplesRead > 1) {
          outOfRange = info.validSamples <= 0 || info.dataOutOfRange || info.signalTimeout;
          if (outOfRange) return false;
          if (samplesRead == 3) {
            const int64_t previousDelta = (int64_t)previousSample - wowRtc.baselineRaw;
            const int64_t currentDelta = (int64_t)info.rawValue - wowRtc.baselineRaw;
            const bool increased = previousDelta > wowRtc.thresholdRaw && currentDelta > wowRtc.thresholdRaw;
            const bool decreased = previousDelta < -(int64_t)wowRtc.thresholdRaw && currentDelta < -(int64_t)wowRtc.thresholdRaw;
            rawSample = increased || decreased ? info.rawValue : wowRtc.baselineRaw;
            return true;
          }
          previousSample = info.rawValue;
        }
      }
    }
    delay(2);
  }
  return false;
}

static void wowLatchMicroPins() {
  pinMode(SCALE_SCLK, OUTPUT); digitalWrite(SCALE_SCLK, LOW);
  gpio_hold_en((gpio_num_t)SCALE_SCLK);
  pinMode(SCALE_PDWN, OUTPUT); digitalWrite(SCALE_PDWN, LOW);
  gpio_hold_en((gpio_num_t)SCALE_PDWN);
  pinMode(SCALE_DOUT, INPUT);
  gpio_hold_en((gpio_num_t)SCALE_DOUT);
  digitalWrite(PWR_CTRL, LOW);
  gpio_hold_en((gpio_num_t)PWR_CTRL);
  gpio_deep_sleep_hold_en();
}

static void wowSetCpuFrequencyMhz(unsigned long frequencyMhz) {
#ifdef CONFIG_PM_ENABLE
  (void)frequencyMhz;
#else
  setCpuFrequencyMhz(frequencyMhz);
#endif
}

void wowMicroWakeOrContinue() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
  if (wowRtc.magic != WOW_RTC_MAGIC || !wowRtc.armed) return;
  configureWakePinsForDeepSleep();
  if (wowPhysicalWakeRequested()) {
    wowRtc.armed = 0;
    return;
  }
  const unsigned long bootFreqMhz = getCpuFrequencyMhz();
  wowSetCpuFrequencyMhz(20);
  gpio_hold_dis((gpio_num_t)SCALE_SCLK);
  gpio_hold_dis((gpio_num_t)SCALE_PDWN);
  gpio_hold_dis((gpio_num_t)SCALE_DOUT);
  gpio_hold_dis((gpio_num_t)PWR_CTRL);
  gpio_deep_sleep_hold_dis();

  pinMode(PWR_CTRL, OUTPUT);
  digitalWrite(PWR_CTRL, HIGH);
  if (!wowWaitForRail()) {
    wowRtc.armed = 0;
    wowSetCpuFrequencyMhz(bootFreqMhz);
    return;
  }

  ADS1232_ADC wowAdc(SCALE_DOUT, SCALE_SCLK, SCALE_PDWN, SCALE_A0);
  wowAdc.begin();
  bool outOfRange = false;
  int32_t rawSample = 0;
  const bool gotSample =
      wowReadWakeSamples(wowAdc, rawSample, outOfRange);
  wowAdc.powerDown();
  wowAdc.end();

  if (GPIO_power_on_with >= 0 || wowPhysicalWakeRequested()) {
    wowRtc.armed = 0;
    wowSetCpuFrequencyMhz(bootFreqMhz);
    return;
  }

  if (gotSample && !outOfRange) {
    const int64_t delta = rawSample > wowRtc.baselineRaw
                              ? (int64_t)rawSample - wowRtc.baselineRaw
                              : (int64_t)wowRtc.baselineRaw - rawSample;
    if (delta > wowRtc.thresholdRaw) {
      wowRtc.armed = 0;
      wowSetCpuFrequencyMhz(bootFreqMhz);
      return;
    }
  }

  configureWakePinsForDeepSleep();
  esp_sleep_enable_ext1_wakeup_io(PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);
  if (wowPhysicalWakeRequested()) {
    wowRtc.armed = 0;
    wowSetCpuFrequencyMhz(bootFreqMhz);
    return;
  }
  wowLatchMicroPins();
  if (wowRtc.armed) esp_sleep_enable_timer_wakeup(wowRtc.intervalUs);
  esp_deep_sleep_start();
}

#else  // !ADS1232ADC

inline void wowCaptureBaselineForSleep() {}
inline void wowArmSleepTimer() {}
inline void wowMicroWakeOrContinue() {}

#endif  // ADS1232ADC

#endif  // WAKE_ON_WEIGHT_H
