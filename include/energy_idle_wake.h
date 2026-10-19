#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_pm.h>
#include <esp_sleep.h>

static bool energyLightSleepWakePinSupported(gpio_num_t pin) {
  return rtc_gpio_is_valid_gpio(pin);
}

static uint64_t energyLightSleepWakePinMask(gpio_num_t pin) {
  return energyLightSleepWakePinSupported(pin) ? 1ULL << pin : 0;
}

static uint64_t energyLightSleepSupportedWakePinMask() {
  uint64_t mask = energyLightSleepWakePinMask((gpio_num_t)SCALE_DOUT) |
                  energyLightSleepWakePinMask((gpio_num_t)BUTTON_CIRCLE) |
                  energyLightSleepWakePinMask((gpio_num_t)BUTTON_SQUARE);
#ifdef USB_DET
  mask |= energyLightSleepWakePinMask((gpio_num_t)USB_DET);
#endif
  return mask;
}

static uint64_t energyLightSleepRequiredWakePinMask(bool scaleWakeEnabled,
                                                     bool usbWakeEnabled) {
  uint64_t mask = energyLightSleepWakePinMask((gpio_num_t)BUTTON_CIRCLE) |
                  energyLightSleepWakePinMask((gpio_num_t)BUTTON_SQUARE);
  if (scaleWakeEnabled) {
    mask |= energyLightSleepWakePinMask((gpio_num_t)SCALE_DOUT);
  }
#ifdef USB_DET
  if (usbWakeEnabled) {
    mask |= energyLightSleepWakePinMask((gpio_num_t)USB_DET);
  }
#endif
  return mask;
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
                                                 bool usbWakeEnabled,
                                                 uint64_t *enabledMask) {
  const uint64_t supportedMask = energyLightSleepSupportedWakePinMask();
  esp_err_t result = ESP_OK;
  if (supportedMask != 0) {
    result = esp_sleep_disable_ext1_wakeup_io(supportedMask);
  }
  if (result == ESP_OK && enabled) {
    const uint64_t wakeMask = energyLightSleepRequiredWakePinMask(
      scaleWakeEnabled, usbWakeEnabled);
    if (wakeMask != 0) {
      result = esp_sleep_enable_ext1_wakeup_io(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
    }
    if (result == ESP_OK && enabledMask != nullptr) *enabledMask = wakeMask;
  } else if (result == ESP_OK && enabledMask != nullptr) {
    *enabledMask = 0;
  }
  if (result != ESP_OK) {
    const uint64_t wakeMask = energyLightSleepRequiredWakePinMask(
      scaleWakeEnabled, usbWakeEnabled);
    if (wakeMask != 0) esp_sleep_disable_ext1_wakeup_io(wakeMask);
    return false;
  }
  return true;
}

static bool restoreEnergyLightSleepWakePins(uint64_t wakeMask) {
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
    if ((wakeMask & energyLightSleepWakePinMask(pin)) == 0) continue;
    const esp_err_t holdResult = rtc_gpio_hold_dis(pin);
    const esp_err_t deinitResult = rtc_gpio_deinit(pin);
    restored = holdResult == ESP_OK && deinitResult == ESP_OK && restored;
  }
  return restored;
}
