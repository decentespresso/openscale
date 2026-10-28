#ifndef FUEL_GAUGE_INTERNAL_H
#define FUEL_GAUGE_INTERNAL_H
#include <Arduino.h>

constexpr uint8_t gaugeAddress = 0x55;
constexpr uint16_t gaugePorFlag = 0x0020;
constexpr uint16_t gaugeConfigFlag = 0x0010;

bool gaugeReadWord(uint8_t reg, uint16_t &value);
bool gaugeReadControl(uint16_t command, uint16_t &value);
bool gaugeConfigure(uint16_t &capacity, bool adoptCapacity);

#endif
