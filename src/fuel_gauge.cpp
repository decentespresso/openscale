#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include "config.h"
#include <Wire.h>
#ifdef ESP32
#include "driver/gpio.h"
#endif

void gaugeSetCharging(bool enabled) {
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

static void gaugeSelectHardware() {
  fuelGaugeRuntime.hardware = BatteryHardware::Gauge;
  b_hasFuelGauge = true;
#ifdef ESP32
  gpio_set_level(static_cast<gpio_num_t>(CHRG_CTRL), HIGH);
#endif
  pinMode(CHRG_CTRL, OUTPUT);
  digitalWrite(CHRG_CTRL, HIGH);
#ifdef ESP32
  gpio_hold_dis(static_cast<gpio_num_t>(CHRG_CTRL));
#endif
  fuelGaugeRuntime.chargeEnabled = false;
  pinMode(GPOUT, INPUT);
}

void fuelGaugeSetResealPending(bool pending) {
  fuelGaugeRuntime.resealPending = pending;
  fuelGaugeRuntime.sealRequired = pending;
  if (pending && !b_hasFuelGauge) fuelGaugeRuntime.hardware = BatteryHardware::Unknown;
}

void fuelGaugeStartDetection(uint16_t capacity, bool adoptCapacity) {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  runtime.desiredCapacity = capacity;
  runtime.adoptCapacity = adoptCapacity;
  runtime.probeAttempts = 0;
  runtime.probeComplete = false;
  runtime.probeAbsent = true;
  runtime.quiesceRequested = false;
  runtime.quiesceComplete = false;
  runtime.quiesceFailed = false;
  runtime.configurationPending = false;
  if (!b_hasFuelGauge) runtime.hardware = BatteryHardware::Unknown;
  Wire.begin(FUEL_GAUGE_I2C_SDA, FUEL_GAUGE_I2C_SCL);
  Wire.setTimeOut(50);
}

bool fuelGaugePollDetection() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  if (runtime.probeComplete || runtime.quiesceRequested) return true;
  if (runtime.probeAttempts > 0 && millis() - runtime.lastProbe < 200) return false;
  delayMicroseconds(70);
  Wire.beginTransmission(gaugeAddress);
  const uint8_t result = Wire.endTransmission(true);
  runtime.lastProbe = millis();
  runtime.probeAbsent = runtime.probeAbsent && result == 2;
  ++runtime.probeAttempts;
  if (result == 0) {
    gaugeSelectHardware();
    runtime.configurationPending = true;
    runtime.ready = false;
    runtime.probeComplete = true;
    return true;
  }
  if (runtime.probeAttempts < 5) return false;
  if (b_hasFuelGauge) {
    fuelGaugePauseCharging();
    runtime.ready = false;
  } else {
    runtime.hardware = runtime.probeAbsent && !runtime.resealPending && !runtime.sealRequired
        ? BatteryHardware::Legacy : BatteryHardware::Unknown;
  }
  runtime.lastRecovery = millis();
  runtime.probeComplete = true;
  return true;
}

bool fuelGaugeBegin(uint16_t capacity, bool adoptCapacity) {
  fuelGaugeStartDetection(capacity, adoptCapacity);
  while (!fuelGaugePollDetection()) delay(1);
  if (fuelGaugeRuntime.configurationAllowed && b_hasFuelGauge) return fuelGaugeDrain();
  return false;
}

void gaugeApplyProtection() {
  const FuelGaugeReading reading = fuelGaugeReading();
  if (!fuelGaugeRuntime.ready || !reading.valid || fuelGaugeRuntime.resealPending ||
      fuelGaugeRuntime.quiesceRequested || fuelGaugeRuntime.configurationPending) {
    gaugeSetCharging(false);
    return;
  }
  if (!b_batteryProtect || reading.soc <= 75) gaugeSetCharging(true);
  else if (reading.soc >= 80) gaugeSetCharging(false);
}

bool fuelGaugeBusy() {
  return fuelGaugeRuntime.engine.stage != GaugeStage::Idle;
}

void fuelGaugeAllowConfiguration() {
  fuelGaugeRuntime.configurationAllowed = true;
}

void fuelGaugeRequestQuiesce() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  if (runtime.quiesceRequested) return;
  runtime.quiesceRequested = true;
  runtime.quiesceComplete = false;
  runtime.quiesceFailed = false;
  fuelGaugePauseCharging();
  if (runtime.engine.stage == GaugeStage::Sample) {
    runtime.engine.io.kind = GaugeIoKind::None;
    runtime.engine.stage = GaugeStage::Idle;
  }
  if (!fuelGaugeBusy() && !runtime.resealPending && !runtime.sealRequired) {
    runtime.quiesceComplete = true;
    return;
  }
  if (!fuelGaugeBusy() && b_hasFuelGauge) gaugeStartOperation(true);
  runtime.engine.cleanupOnly = true;
  runtime.engine.cleanupStarted = true;
  runtime.engine.cleanupStartedAt = millis();
}

FuelGaugeQuiesceStatus fuelGaugeQuiesceStatus() {
  if (!fuelGaugeRuntime.quiesceRequested) return FuelGaugeQuiesceStatus::Running;
  if (fuelGaugeRuntime.quiesceFailed) return FuelGaugeQuiesceStatus::Failed;
  return fuelGaugeRuntime.quiesceComplete ? FuelGaugeQuiesceStatus::Complete : FuelGaugeQuiesceStatus::Pending;
}

void fuelGaugeResume() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  runtime.quiesceRequested = false;
  runtime.quiesceComplete = false;
  runtime.quiesceFailed = false;
  fuelGaugePauseCharging();
#ifdef ESP32
  if (b_hasFuelGauge) gpio_hold_dis(static_cast<gpio_num_t>(CHRG_CTRL));
#endif
  runtime.lastPoll = millis() - 1000;
  if (b_hasFuelGauge && !runtime.ready) runtime.configurationPending = true;
}

void fuelGaugeLoop() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  gaugeApplyProtection();
  if (runtime.quiesceRequested && (runtime.quiesceComplete || runtime.quiesceFailed)) return;
  if (!runtime.probeComplete && !runtime.quiesceRequested) {
    fuelGaugePollDetection();
    return;
  }
  if (runtime.quiesceRequested && !b_hasFuelGauge) {
    if (millis() - runtime.engine.cleanupStartedAt >= 8000) runtime.quiesceFailed = true;
    return;
  }
  if (!b_hasFuelGauge) {
    if ((runtime.resealPending || runtime.sealRequired) && millis() - runtime.lastRecovery >= 10000) {
      fuelGaugeStartDetection(runtime.desiredCapacity, runtime.adoptCapacity);
    }
    return;
  }
  if (runtime.recoveryPending && millis() - runtime.lastRecovery < 10000) return;
  if (fuelGaugeBusy()) {
    gaugeServiceOperation();
    return;
  }
  if (runtime.resealPending || runtime.sealRequired) {
    if (runtime.configurationPending || runtime.quiesceRequested || millis() - runtime.lastRecovery >= 10000) gaugeStartOperation(true);
    return;
  }
  if (runtime.quiesceRequested) {
    runtime.quiesceComplete = true;
    return;
  }
  if (!runtime.configurationAllowed) return;
  if (runtime.configurationPending || (!runtime.ready && millis() - runtime.lastRecovery >= 10000)) {
    gaugeStartOperation(false);
    return;
  }
  if (!runtime.ready || millis() - runtime.lastPoll < 1000) return;
  runtime.lastPoll = millis();
  runtime.pendingReading = FuelGaugeReading{};
  runtime.engine = FuelGaugeEngine{};
  runtime.engine.stage = GaugeStage::Sample;
  runtime.engine.sampleStartedAt = millis();
}

bool fuelGaugeDrain() {
  if (!b_hasFuelGauge || !fuelGaugeRuntime.configurationAllowed ||
      fuelGaugeRuntime.quiesceRequested || fuelGaugeRuntime.recoveryPending) return false;
  do {
    fuelGaugeLoop();
    if (fuelGaugeBusy() || fuelGaugeRuntime.configurationPending) delay(1);
  } while ((fuelGaugeBusy() || fuelGaugeRuntime.configurationPending) && !fuelGaugeRuntime.recoveryPending);
  return fuelGaugeRuntime.ready;
}

void fuelGaugeProtectSet(bool enable) {
  b_batteryProtect = enable;
  gaugeApplyProtection();
}

bool fuelGaugeSetCapacity(uint16_t capacity) {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  if (!b_hasFuelGauge || capacity < 300 || capacity > 2000) return false;
  const bool busy = fuelGaugeBusy() || runtime.configurationPending || runtime.recoveryPending || runtime.quiesceRequested || !runtime.configurationAllowed;
  runtime.desiredCapacity = capacity;
  runtime.adoptCapacity = false;
  runtime.configurationPending = true;
  fuelGaugePauseCharging();
  return !busy && fuelGaugeDrain();
}

uint16_t fuelGaugeDesignCapacityMah() { return fuelGaugeRuntime.ready ? fuelGaugeRuntime.desiredCapacity : 0; }
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
  fuelGaugePauseCharging();
#ifdef ESP32
  gpio_hold_en(static_cast<gpio_num_t>(CHRG_CTRL));
#endif
}
