#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline void gpio_hold_dis(gpio_num_t pin) { pinHolds[pin] = false; }
inline void gpio_hold_en(gpio_num_t pin) { pinHolds[pin] = true; }
inline void gpio_deep_sleep_hold_en() {}
