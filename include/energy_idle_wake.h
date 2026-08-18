#pragma once

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_pm.h>
#include <esp_sleep.h>

constexpr uint64_t ENERGY_IDLE_WAKE_PIN_MASK =
  (1ULL << SCALE_DOUT) |
  (1ULL << BUTTON_CIRCLE) |
  (1ULL << BUTTON_SQUARE)
#ifdef USB_DET
  | (1ULL << USB_DET)
#endif
  ;

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
  ESP_ERROR_CHECK(rtc_gpio_hold_dis((gpio_num_t)SCALE_DOUT));
  ESP_ERROR_CHECK(rtc_gpio_deinit((gpio_num_t)SCALE_DOUT));
  ESP_ERROR_CHECK(rtc_gpio_hold_dis((gpio_num_t)BUTTON_CIRCLE));
  ESP_ERROR_CHECK(rtc_gpio_deinit((gpio_num_t)BUTTON_CIRCLE));
  ESP_ERROR_CHECK(rtc_gpio_hold_dis((gpio_num_t)BUTTON_SQUARE));
  ESP_ERROR_CHECK(rtc_gpio_deinit((gpio_num_t)BUTTON_SQUARE));
#ifdef USB_DET
  ESP_ERROR_CHECK(rtc_gpio_hold_dis((gpio_num_t)USB_DET));
  ESP_ERROR_CHECK(rtc_gpio_deinit((gpio_num_t)USB_DET));
#endif
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) return ESP_OK;
  TaskHandle_t mainTask = static_cast<TaskHandle_t>(context);
  if (mainTask != nullptr) xTaskNotifyGive(mainTask);
  return ESP_OK;
}

static void registerEnergyMainLoopLightSleepCallback(TaskHandle_t mainTask) {
  esp_pm_sleep_cbs_register_config_t callbacks = {};
  callbacks.exit_cb = energyMainLoopWakeAfterLightSleep;
  callbacks.exit_cb_user_arg = mainTask;
  ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&callbacks));
}

static void setEnergyLightSleepWakeSourceEnabled(bool enabled) {
  const esp_err_t result = enabled
    ? esp_sleep_enable_ext1_wakeup_io(ENERGY_IDLE_WAKE_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW)
    : esp_sleep_disable_ext1_wakeup_io(ENERGY_IDLE_WAKE_PIN_MASK);
  ESP_ERROR_CHECK(result);
}
