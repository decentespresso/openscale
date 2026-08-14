// BQ27427 fuel gauge compat layer for HDS 9.0.5.
// 8.3.1 boards have no gauge: every call no-ops and reports absent.
#ifndef FUEL_GAUGE_H
#define FUEL_GAUGE_H
#include <Arduino.h>

extern volatile bool b_hasFuelGauge;

#ifndef FUEL_GAUGE_I2C_SDA
#define FUEL_GAUGE_I2C_SDA 5
#endif
#ifndef FUEL_GAUGE_I2C_SCL
#define FUEL_GAUGE_I2C_SCL 4
#endif
#ifndef FUEL_GAUGE_USB_DET
#define FUEL_GAUGE_USB_DET 8
#endif

bool fuelGaugeBegin();
float fuelGaugeVoltageV();
int16_t fuelGaugeCurrentMa();
int16_t fuelGaugeStandbyMa();
int16_t fuelGaugeMaxLoadMa();
int16_t fuelGaugePowerMw();
uint16_t fuelGaugeCapacityMah();
uint16_t fuelGaugeFullCapacityMah();
uint8_t fuelGaugeSoHPercent();
uint8_t fuelGaugeSocPercent();
float fuelGaugeTempC();
bool fuelGaugeCharging();
bool fuelGaugeFullCharged();
bool fuelGaugeDischarging();
bool fuelGaugeChgAllowed();
bool fuelGaugeUsbPlugged();
void fuelGaugeSleep();

#endif
