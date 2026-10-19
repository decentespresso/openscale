#ifndef FUEL_GAUGE_SLEEP_H
#define FUEL_GAUGE_SLEEP_H

#include "fuel_gauge.h"
#include <Wire.h>
#include <esp_sleep.h>

static uint64_t fuelGaugeSleepWowInterval() {
#ifdef ADS1232ADC
  return wowRtc.magic == WOW_RTC_MAGIC && wowRtc.armed ? wowRtc.intervalUs : 0;
#else
  return 0;
#endif
}

static bool fuelGaugeSleepUsbPresent() {
  configureWakePinForDeepSleep(static_cast<gpio_num_t>(FUEL_GAUGE_USB_DET));
  return rtc_gpio_get_level(static_cast<gpio_num_t>(FUEL_GAUGE_USB_DET)) == LOW;
}

uint64_t fuelGaugeSleepWakeMask() {
  if (!fuelGaugeDeepSleepActive()) return PIN_BITMASK;
  const uint64_t buttons = BUTTON_PIN_BITMASK(BUTTON_CIRCLE) | BUTTON_PIN_BITMASK(BUTTON_SQUARE);
  return buttons | (fuelGaugeSleepUsbPresent() ? 0 : BUTTON_PIN_BITMASK(FUEL_GAUGE_USB_DET));
}

static void fuelGaugeArmSleepTimer() {
  if (!fuelGaugeDeepSleepActive()) return;
  const uint64_t wowInterval = fuelGaugeSleepWowInterval();
  const uint64_t chargeInterval = fuelGaugeSleepUsbPresent() ? 10000000ULL : 0;
  const uint64_t interval = wowInterval != 0 && chargeInterval != 0
      ? (wowInterval < chargeInterval ? wowInterval : chargeInterval)
      : wowInterval + chargeInterval;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  if (interval != 0) esp_sleep_enable_timer_wakeup(interval);
}

static void fuelGaugeConfigureSleepWake() {
  const uint64_t allWakePins = PIN_BITMASK | BUTTON_PIN_BITMASK(FUEL_GAUGE_USB_DET);
  esp_sleep_disable_ext1_wakeup_io(allWakePins);
  esp_sleep_enable_ext1_wakeup_io(fuelGaugeSleepWakeMask(), ESP_EXT1_WAKEUP_ANY_LOW);
  fuelGaugeArmSleepTimer();
}

static int fuelGaugeSleepButton() {
  if (rtc_gpio_get_level(static_cast<gpio_num_t>(BUTTON_CIRCLE)) == LOW) return BUTTON_CIRCLE;
  if (rtc_gpio_get_level(static_cast<gpio_num_t>(BUTTON_SQUARE)) == LOW) return BUTTON_SQUARE;
  return -1;
}

static void fuelGaugeHoldChargingForBoot() {
  digitalWrite(CHRG_CTRL, HIGH);
  pinMode(CHRG_CTRL, OUTPUT);
  gpio_hold_dis(static_cast<gpio_num_t>(CHRG_CTRL));
  gpio_hold_en(static_cast<gpio_num_t>(CHRG_CTRL));
  fuelGaugeSleepState.chargeEnabled = false;
}

int fuelGaugeMicroWakeOrContinue() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (!fuelGaugeDeepSleepActive()) return -1;
  if (cause != ESP_SLEEP_WAKEUP_TIMER && cause != ESP_SLEEP_WAKEUP_EXT1) {
    fuelGaugeSleepState.magic = 0;
    return -1;
  }
  configureWakePinsForDeepSleep();
  const uint64_t buttons = BUTTON_PIN_BITMASK(BUTTON_CIRCLE) | BUTTON_PIN_BITMASK(BUTTON_SQUARE);
  if (cause == ESP_SLEEP_WAKEUP_EXT1 && (esp_sleep_get_ext1_wakeup_status() & buttons)) {
    fuelGaugeHoldChargingForBoot();
    return -1;
  }
  const int button = fuelGaugeSleepButton();
  if (button > 0) {
    fuelGaugeHoldChargingForBoot();
    return button;
  }
  b_hasFuelGauge = true;
  b_batteryProtect = true;
  fuelGaugeRuntime.hardware = BatteryHardware::Gauge;
  fuelGaugeRuntime.desiredCapacity = fuelGaugeSleepState.capacity;
  fuelGaugeRuntime.ready = fuelGaugeSleepState.ready;
  fuelGaugeRuntime.chargeEnabled = fuelGaugeSleepState.chargeEnabled;
  fuelGaugeRuntime.sealRequired = fuelGaugeSleepState.sealRequired;
  fuelGaugeRuntime.lastPoll = millis() - 1000;
  fuelGaugeRuntime.lastRecovery = millis() - 10000;
  fuelGaugeRuntime.lowSocNotified = true;
  digitalWrite(CHRG_CTRL, fuelGaugeRuntime.chargeEnabled ? LOW : HIGH);
  pinMode(CHRG_CTRL, OUTPUT);
  gpio_hold_dis(static_cast<gpio_num_t>(CHRG_CTRL));
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  gpio_hold_dis(static_cast<gpio_num_t>(I2C_SDA));
  gpio_hold_dis(static_cast<gpio_num_t>(I2C_SCL));
  Wire.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  fuelGaugeLoop();
  Wire.end();
  fuelGaugeSleep();
  pinMode(I2C_SDA, OUTPUT);
  digitalWrite(I2C_SDA, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(I2C_SDA));
  pinMode(I2C_SCL, OUTPUT);
  digitalWrite(I2C_SCL, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(I2C_SCL));
  const int pendingButton = fuelGaugeSleepButton();
  if (pendingButton > 0) {
    fuelGaugeHoldChargingForBoot();
    return pendingButton;
  }
  fuelGaugeConfigureSleepWake();
  gpio_deep_sleep_hold_en();
  if (cause == ESP_SLEEP_WAKEUP_TIMER && fuelGaugeSleepWowInterval() != 0) return -1;
  esp_deep_sleep_start();
  return -1;
}

#endif
