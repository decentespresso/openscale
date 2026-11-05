#ifndef FUEL_GAUGE_INTERNAL_H
#define FUEL_GAUGE_INTERNAL_H
#include <Arduino.h>
#include "fuel_gauge_engine.h"

constexpr uint8_t gaugeAddress = 0x55;
constexpr uint16_t gaugePorFlag = 0x0020;
constexpr uint16_t gaugeConfigFlag = 0x0010;

int gaugeIoRun(GaugeIoKind kind, uint16_t argument, const uint8_t *bytes = nullptr);
void gaugeStartOperation(bool cleanupOnly);
void gaugeServiceOperation();
void gaugeSetCharging(bool enabled);
void gaugeApplyProtection();
bool gaugeConfigure(uint16_t &capacity, bool adoptCapacity);

#endif
