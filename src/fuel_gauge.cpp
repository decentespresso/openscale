#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include "config.h"
#include <Wire.h>
#ifdef ESP32
#include "driver/gpio.h"
#endif

static void gaugeSetCharging(bool enabled) {
  if (!b_hasFuelGauge || fuelGaugeRuntime.chargeEnabled == enabled) return;
  digitalWrite(CHRG_CTRL, enabled ? LOW : HIGH);
  fuelGaugeRuntime.chargeEnabled = enabled;
}

bool fuelGaugeLegacyHardware() {
  return fuelGaugeRuntime.hardware == BatteryHardware::Legacy;
}

FuelGaugeReading fuelGaugeReading() {
  FuelGaugeReading reading = fuelGaugeRuntime.reading;
  reading.valid = reading.valid && millis() - reading.sampledAt < 2500;
  return reading;
}

void fuelGaugePauseCharging() {
  gaugeSetCharging(false);
  fuelGaugeRuntime.reading.valid = false;
}

static bool gaugeRecover() {
  gaugeSetCharging(false);
  fuelGaugeRuntime.reading.valid = false;
  uint16_t deviceType;
  const uint16_t previousCapacity = fuelGaugeRuntime.desiredCapacity;
  fuelGaugeRuntime.ready = gaugeReadControl(0x0001, deviceType) &&
                          deviceType == 0x0427 &&
                          gaugeConfigure(fuelGaugeRuntime.desiredCapacity,
                                         fuelGaugeRuntime.adoptCapacity);
  if (previousCapacity == 0 && fuelGaugeRuntime.desiredCapacity != 0) {
    fuelGaugeRuntime.capacityPersistPending = true;
  }
  fuelGaugeRuntime.lastRecovery = millis();
  fuelGaugeRuntime.lastPoll = millis();
  return fuelGaugeRuntime.ready;
}

bool fuelGaugeBegin(uint16_t capacity, bool adoptCapacity) {
  fuelGaugeRuntime.desiredCapacity = capacity;
  fuelGaugeRuntime.adoptCapacity = adoptCapacity;
  Wire.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt > 0) delay(200);
    delayMicroseconds(70);
    Wire.beginTransmission(gaugeAddress);
    if (Wire.endTransmission(true) == 0) {
      fuelGaugeRuntime.hardware = BatteryHardware::Gauge;
      b_hasFuelGauge = true;
      digitalWrite(CHRG_CTRL, HIGH);
      pinMode(CHRG_CTRL, OUTPUT);
#ifdef ESP32
      gpio_hold_dis(static_cast<gpio_num_t>(CHRG_CTRL));
#endif
      pinMode(GPOUT, INPUT);
      const bool ready = gaugeRecover();
      Serial.println(ready ? "fuelGauge: ready" : "fuelGauge: unavailable, charging disabled");
      return ready;
    }
  }
  fuelGaugeRuntime.hardware = BatteryHardware::Legacy;
  b_hasFuelGauge = false;
  return false;
}

static void gaugeApplyProtection() {
  const FuelGaugeReading reading = fuelGaugeReading();
  if (!fuelGaugeRuntime.ready || !reading.valid) {
    gaugeSetCharging(false);
    return;
  }
  if (!b_batteryProtect || reading.soc <= 75) {
    gaugeSetCharging(true);
  } else if (reading.soc >= 80) {
    gaugeSetCharging(false);
  }
}

void fuelGaugeLoop() {
  if (!b_hasFuelGauge) return;
  gaugeApplyProtection();
  const unsigned long now = millis();
  if (now - fuelGaugeRuntime.lastPoll < 1000) return;
  fuelGaugeRuntime.lastPoll = now;
  if (!fuelGaugeRuntime.ready) {
    if (now - fuelGaugeRuntime.lastRecovery >= 10000) {
      gaugeRecover();
    }
    return;
  }
  FuelGaugeReading reading;
  if (!gaugeReadWord(0x06, reading.flags)) {
    fuelGaugePauseCharging();
    return;
  }
  if (reading.flags & (gaugePorFlag | gaugeConfigFlag)) {
    gaugeRecover();
    return;
  }
  uint16_t current;
  uint16_t power;
  const bool valid = gaugeReadWord(0x04, reading.voltageMv) &&
                     gaugeReadWord(0x1c, reading.soc) &&
                     gaugeReadWord(0x1e, reading.temperature) &&
                     gaugeReadWord(0x10, current) &&
                     gaugeReadWord(0x18, power) &&
                     gaugeReadWord(0x0c, reading.capacityMah) &&
                     gaugeReadWord(0x0e, reading.fullCapacityMah) &&
                     gaugeReadWord(0x20, reading.soh);
  if (!valid || reading.soc > 100 || reading.voltageMv == 0 ||
      reading.voltageMv > 5000 || (reading.soh & 0xff) > 100 ||
      reading.temperature < 1731 || reading.temperature > 4732) {
    fuelGaugePauseCharging();
    return;
  }
  reading.currentMa = static_cast<int16_t>(current);
  reading.powerMw = static_cast<int16_t>(power);
  reading.soh &= 0xff;
  reading.valid = true;
  reading.sequence = fuelGaugeRuntime.reading.sequence + 1;
  reading.sampledAt = millis();
  fuelGaugeRuntime.reading = reading;
  if (reading.soc < 10 && !fuelGaugeRuntime.lowSocNotified) {
    Serial.printf("fuelGauge: battery low, SOC %u%%\n", reading.soc);
  }
  fuelGaugeRuntime.lowSocNotified = reading.soc < 10;
  gaugeApplyProtection();
}

void fuelGaugeProtectSet(bool enable) {
  b_batteryProtect = enable;
  gaugeApplyProtection();
}

bool fuelGaugeSetCapacity(uint16_t capacity) {
  if (!b_hasFuelGauge || capacity < 300 || capacity > 2000) return false;
  fuelGaugeRuntime.desiredCapacity = capacity;
  return gaugeRecover();
}

uint16_t fuelGaugeDesignCapacityMah() {
  return fuelGaugeRuntime.ready ? fuelGaugeRuntime.desiredCapacity : 0;
}

float fuelGaugeVoltageV() { return fuelGaugeRuntime.reading.voltageMv / 1000.0f; }
int16_t fuelGaugeCurrentMa() { return fuelGaugeRuntime.reading.currentMa; }
int16_t fuelGaugePowerMw() { return fuelGaugeRuntime.reading.powerMw; }
uint16_t fuelGaugeCapacityMah() { return fuelGaugeRuntime.reading.capacityMah; }
uint16_t fuelGaugeFullCapacityMah() { return fuelGaugeRuntime.reading.fullCapacityMah; }
uint8_t fuelGaugeSoHPercent() { return fuelGaugeRuntime.reading.soh; }
uint8_t fuelGaugeSocPercent() { return fuelGaugeRuntime.reading.soc; }
float fuelGaugeTempC() { return fuelGaugeRuntime.reading.temperature * 0.1f - 273.15f; }
bool fuelGaugeCharging() { return digitalRead(FUEL_GAUGE_CHRG_DET) == LOW; }
bool fuelGaugeFullCharged() { return (fuelGaugeRuntime.reading.flags & 0x0200) != 0; }
bool fuelGaugeDischarging() { return (fuelGaugeRuntime.reading.flags & 0x0001) != 0; }
bool fuelGaugeChgAllowed() { return (fuelGaugeRuntime.reading.flags & 0x0100) != 0; }
bool fuelGaugeUsbPlugged() { return digitalRead(FUEL_GAUGE_USB_DET) == LOW; }

void fuelGaugeSleep() {
  if (!b_hasFuelGauge) return;
  gaugeApplyProtection();
  if (b_batteryProtect) gaugeSetCharging(false);
#ifdef ESP32
  gpio_hold_en(static_cast<gpio_num_t>(CHRG_CTRL));
#endif
}
