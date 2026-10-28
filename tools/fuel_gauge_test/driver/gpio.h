#pragma once
#include <Arduino.h>
using gpio_num_t = int;
inline void gpio_hold_dis(gpio_num_t pin) { pinHolds[pin] = false; }
inline void gpio_hold_en(gpio_num_t pin) { pinHolds[pin] = true; }
