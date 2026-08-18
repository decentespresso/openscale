#pragma once

#include <Arduino.h>

static void IRAM_ATTR energyMainLoopWakeIsr(void *context) {
  TaskHandle_t mainTask = static_cast<TaskHandle_t>(context);
  if (mainTask == nullptr) return;
  BaseType_t higherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(mainTask, &higherPriorityTaskWoken);
  if (higherPriorityTaskWoken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}
