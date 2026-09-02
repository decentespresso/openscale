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
const uint32_t WOW_RTC_MAGIC = 0x574F5731;
const uint8_t WOW_INTERVAL_COUNT = 4;
const uint64_t wowIntervalUs[WOW_INTERVAL_COUNT] = { 0, 2000000, 3000000, 4000000 };

struct WowRtcState {
  uint32_t magic;
  uint8_t armed;
  int32_t baselineRaw;
  int32_t thresholdRaw;
  uint32_t intervalUs;
  uint32_t tickCount;
  int32_t lastSampleRaw;
  int32_t lastDeltaRaw;
};
RTC_DATA_ATTR WowRtcState wowRtc;

void wowCaptureBaselineForSleep() {
  wowRtc.armed = 0;
  if (i_wow_interval <= 0 || i_lowBatteryCount > 0) return;
  if (f_calibration_value == CALIBRATION_VALUE_DEFAULT) return;
  if (scale.getDebugInfo().validSamples <= 0) return;
  wowRtc.magic = WOW_RTC_MAGIC;
  wowRtc.armed = 1;
  wowRtc.baselineRaw = scale.getDebugInfo().smoothedValue;
  wowRtc.thresholdRaw =
      max((int32_t)(WOW_TRIGGER_GRAMS * fabsf(f_calibration_value) + 0.5f),
          WOW_MIN_THRESHOLD_RAW);
  const uint8_t index =
      (i_wow_interval > 0 && i_wow_interval < WOW_INTERVAL_COUNT)
          ? i_wow_interval
          : 0;
  wowRtc.intervalUs = wowIntervalUs[index];
}

static bool wowTimerArmedThisBoot = false;

void wowArmSleepTimer() {
  if (wowRtc.armed && wowRtc.intervalUs > 0) {
    esp_sleep_enable_timer_wakeup(wowRtc.intervalUs);
    wowTimerArmedThisBoot = true;
  } else if (wowTimerArmedThisBoot) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    wowTimerArmedThisBoot = false;
  }
}

static bool wowReadOneSample(ADS1232_ADC &adc, int32_t &rawSample,
                             bool &outOfRange, unsigned long &elapsedMs) {
  const unsigned long startedAt = millis();
  while (millis() - startedAt < WOW_READ_TIMEOUT_MS) {
    if (digitalRead(SCALE_DOUT) == LOW) {
      if (adc.update()) {
        elapsedMs = millis() - startedAt;
        rawSample = adc.getDebugInfo().rawValue;
        outOfRange = adc.getDebugInfo().dataOutOfRange;
        return true;
      }
    }
    delay(2);
  }
  elapsedMs = millis() - startedAt;
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

void wowMicroWakeOrContinue() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
  if (wowRtc.magic != WOW_RTC_MAGIC || !wowRtc.armed) return;
  const unsigned long bootFreqMhz = getCpuFrequencyMhz();
  setCpuFrequencyMhz(20);
  gpio_hold_dis((gpio_num_t)SCALE_SCLK);
  gpio_hold_dis((gpio_num_t)SCALE_PDWN);
  gpio_hold_dis((gpio_num_t)SCALE_DOUT);
  gpio_hold_dis((gpio_num_t)PWR_CTRL);
  gpio_deep_sleep_hold_dis();

  pinMode(PWR_CTRL, OUTPUT);
  digitalWrite(PWR_CTRL, HIGH);
  delay(WOW_RAIL_SETTLE_MS);

  ADS1232_ADC wowAdc(SCALE_DOUT, SCALE_SCLK, SCALE_PDWN, SCALE_A0);
  wowAdc.begin();
  bool outOfRange = false;
  unsigned long elapsedMs = 0;
  int32_t rawSample = 0;
  const bool gotSample =
      wowReadOneSample(wowAdc, rawSample, outOfRange, elapsedMs);
  wowAdc.powerDown();

  if (gotSample && !outOfRange) {
    const int32_t delta = rawSample > wowRtc.baselineRaw
                              ? rawSample - wowRtc.baselineRaw
                              : wowRtc.baselineRaw - rawSample;
    wowRtc.lastSampleRaw = rawSample;
    wowRtc.lastDeltaRaw = delta;
    if (delta > wowRtc.thresholdRaw) {
      wowRtc.armed = 0;
      Serial.printf("[wow] weight detected: delta=%ld > thresh=%ld, full boot\n",
                    (long)delta, (long)wowRtc.thresholdRaw);
      setCpuFrequencyMhz(bootFreqMhz);
      return;
    }
    Serial.printf("[wow] tick=%lu raw=%ld delta=%ld thresh=%ld\n",
                  (unsigned long)wowRtc.tickCount, (long)rawSample,
                  (long)delta, (long)wowRtc.thresholdRaw);
  } else {
    Serial.printf("[wow] tick=%lu no reliable sample, retrying next tick\n",
                  (unsigned long)wowRtc.tickCount);
  }

  configureWakePinsForDeepSleep();
  esp_sleep_enable_ext1_wakeup_io(PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_LOW);
  wowLatchMicroPins();
  esp_sleep_enable_timer_wakeup(wowRtc.intervalUs);
  wowRtc.tickCount++;
  esp_deep_sleep_start();
}

#else  // !ADS1232ADC

inline void wowCaptureBaselineForSleep() {}
inline void wowArmSleepTimer() {}
inline void wowMicroWakeOrContinue() {}

#endif  // ADS1232ADC

#endif  // WAKE_ON_WEIGHT_H
