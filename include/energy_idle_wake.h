#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_pm.h>
#include <esp_sleep.h>

constexpr uint64_t ENERGY_IDLE_BUTTON_WAKE_PIN_MASK =
  (1ULL << BUTTON_CIRCLE) |
  (1ULL << BUTTON_SQUARE);
constexpr uint64_t ENERGY_IDLE_SCALE_WAKE_PIN_MASK = 1ULL << SCALE_DOUT;
#ifdef USB_DET
constexpr uint64_t ENERGY_IDLE_USB_WAKE_PIN_MASK = 1ULL << USB_DET;
#else
constexpr uint64_t ENERGY_IDLE_USB_WAKE_PIN_MASK = 0;
#endif
constexpr uint64_t ENERGY_IDLE_WAKE_PIN_MASK =
  ENERGY_IDLE_BUTTON_WAKE_PIN_MASK |
  ENERGY_IDLE_SCALE_WAKE_PIN_MASK |
  ENERGY_IDLE_USB_WAKE_PIN_MASK;

static bool energyLightSleepWakePinsSupported(bool scaleWakeEnabled,
                                               bool usbWakeEnabled) {
  return rtc_gpio_is_valid_gpio((gpio_num_t)BUTTON_CIRCLE) &&
         rtc_gpio_is_valid_gpio((gpio_num_t)BUTTON_SQUARE) &&
         (!scaleWakeEnabled || rtc_gpio_is_valid_gpio((gpio_num_t)SCALE_DOUT))
#ifdef USB_DET
         && (!usbWakeEnabled || rtc_gpio_is_valid_gpio((gpio_num_t)USB_DET))
#endif
         ;
}

static void IRAM_ATTR energyMainLoopWakeIsr(void *context) {
  TaskHandle_t mainTask = static_cast<TaskHandle_t>(context);
  if (mainTask == nullptr) return;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(mainTask, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

static esp_err_t IRAM_ATTR energyMainLoopWakeAfterLightSleep(int64_t, void *context) {
  EnergyIdleState *state = static_cast<EnergyIdleState *>(context);
  if (state == nullptr) return ESP_OK;
  state->wakePinsNeedRestore = true;
  if (state->mainTask != nullptr) xTaskNotifyGive(state->mainTask);
  return ESP_OK;
}

static bool registerEnergyMainLoopLightSleepCallback(EnergyIdleState *state) {
  esp_pm_sleep_cbs_register_config_t callbacks = {};
  callbacks.exit_cb = energyMainLoopWakeAfterLightSleep;
  callbacks.exit_cb_user_arg = state;
  return esp_pm_light_sleep_register_cbs(&callbacks) == ESP_OK;
}

static bool setEnergyLightSleepWakeSourceEnabled(bool enabled,
                                                 bool scaleWakeEnabled,
                                                 bool usbWakeEnabled) {
  if (enabled && !energyLightSleepWakePinsSupported(scaleWakeEnabled, usbWakeEnabled)) {
    return false;
  }
  esp_err_t result = esp_sleep_disable_ext1_wakeup_io(ENERGY_IDLE_WAKE_PIN_MASK);
  if (result == ESP_OK && enabled) {
    const uint64_t wakeMask = ENERGY_IDLE_BUTTON_WAKE_PIN_MASK |
      (scaleWakeEnabled ? ENERGY_IDLE_SCALE_WAKE_PIN_MASK : 0) |
      (usbWakeEnabled ? ENERGY_IDLE_USB_WAKE_PIN_MASK : 0);
    result = esp_sleep_enable_ext1_wakeup_io(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  if (result != ESP_OK) {
    esp_sleep_disable_ext1_wakeup_io(ENERGY_IDLE_WAKE_PIN_MASK);
    return false;
  }
  return true;
}

static bool restoreEnergyLightSleepWakePins() {
  const gpio_num_t pins[] = {
    (gpio_num_t)SCALE_DOUT,
    (gpio_num_t)BUTTON_CIRCLE,
    (gpio_num_t)BUTTON_SQUARE,
#ifdef USB_DET
    (gpio_num_t)USB_DET,
#endif
  };
  bool restored = true;
  for (const gpio_num_t pin : pins) {
    if (!rtc_gpio_is_valid_gpio(pin)) continue;
    const esp_err_t holdResult = rtc_gpio_hold_dis(pin);
    const esp_err_t deinitResult = rtc_gpio_deinit(pin);
    restored = holdResult == ESP_OK && deinitResult == ESP_OK && restored;
  }
  return restored;
}
