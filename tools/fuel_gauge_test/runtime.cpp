#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include "config.h"
#include <Wire.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#define POWER_H
#define DISPLAY_H
#define AR(text) 100
constexpr int u8g2_font_6x12_tr = 0;
float f_batteryVoltage = 3.8f;
int menuServices = 0;
int batteryPercent() { return fuelGaugeSocPercent(); }
void power_off(int) { ++menuServices; fuelGaugeLoop(); }
struct FakeOled {
  int page = 0, operations = 0, frames = 0;
  bool unavailable = false;
  void firstPage() { page = 0; operations = Wire.operations; ++frames; }
  void setFont(int) {}
  void drawUTF8(int, int, const char *text) {
    if (std::strcmp(text, "Gauge unavailable") == 0) unavailable = true;
  }
  bool nextPage() { assert(operations == Wire.operations); return ++page < 4; }
} u8g2;
#include "fuel_gauge_menu.h"

volatile bool b_hasFuelGauge = false;
volatile bool b_batteryProtect = true;
FuelGaugeRuntime fuelGaugeRuntime;

static void reset() {
  fakeMicros = 0;
  Wire = FakeWire{};
  fuelGaugeRuntime = FuelGaugeRuntime{};
  b_hasFuelGauge = false;
  b_batteryProtect = true;
  pinWrites.clear();
  pinLevels.fill(0);
  pinModes.fill(0);
  pinHolds.fill(false);
  buttonBackAt = std::numeric_limits<unsigned long>::max();
}

static void sample(uint16_t soc) {
  Wire.words[0x1c] = soc;
  delay(1000);
  fuelGaugeLoop();
}

static void checkProbeScheduling() {
  reset();
  assert(fuelGaugePollDetection() && Wire.operations == 0 && fakeMicros == 0);
  Wire.present = false;
  fuelGaugeRuntime.hardware = BatteryHardware::Legacy;
  fuelGaugeStartDetection(700, false);
  assert(!fuelGaugeLegacyHardware() && !b_hasFuelGauge);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt > 0) {
      delay(199);
      const uint64_t before = fakeMicros;
      assert(!fuelGaugePollDetection() && fakeMicros == before);
      assert(Wire.probeTimes.size() == static_cast<size_t>(attempt));
      delay(1);
    }
    const uint64_t before = fakeMicros;
    assert(fuelGaugePollDetection() == (attempt == 4));
    assert(fakeMicros - before == 70);
    assert(Wire.probeTimes.back() == static_cast<unsigned long>(attempt * 200));
    const uint64_t completedAt = fakeMicros;
    for (int index = 0; index < 100; ++index) {
      assert(fuelGaugePollDetection() == (attempt == 4));
    }
    assert(fakeMicros == completedAt && Wire.operations == attempt + 1);
    assert(pinWrites.empty());
  }
  assert(fuelGaugeLegacyHardware() && millis() == 800);

  reset();
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  assert(!fuelGaugePollDetection());
  delay(1000);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 2);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 2);
  delay(199);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 2);
  delay(1);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 3);

  reset();
  Wire.present = false;
  Wire.probeDelayMs = 40;
  fuelGaugeStartDetection(700, false);
  assert(!fuelGaugePollDetection() && millis() == 40);
  delay(199);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 1);
  delay(1);
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 2);
  assert(Wire.probeTimes.back() == 240 && millis() == 280);

  reset();
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  assert(!fuelGaugePollDetection());
  fuelGaugeRuntime.lastProbe = std::numeric_limits<unsigned long>::max() - 100;
  fakeMicros = 98000;
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 1);
  fakeMicros = 99000;
  assert(!fuelGaugePollDetection() && Wire.probeTimes.size() == 2);
}

static void checkDelayedGaugeDetection() {
  for (int acknowledgedAttempt = 2; acknowledgedAttempt <= 5; ++acknowledgedAttempt) {
    reset();
    Wire.probeResults = std::vector<uint8_t>(acknowledgedAttempt - 1, 2);
    Wire.probeResults.push_back(0);
    fuelGaugeStartDetection(700, false);
    for (int attempt = 1; attempt <= acknowledgedAttempt; ++attempt) {
      if (attempt > 1) delay(200);
      assert(fuelGaugePollDetection() == (attempt == acknowledgedAttempt));
      assert(Wire.probeTimes.size() == static_cast<size_t>(attempt));
      assert(Wire.probeTimes.back() == static_cast<unsigned long>((attempt - 1) * 200));
      assert(!fuelGaugeLegacyHardware());
      assert(std::find(pinWrites.begin(), pinWrites.end(), GPOUT) == pinWrites.end());
      if (attempt < acknowledgedAttempt) assert(pinWrites.empty());
    }
    assert(b_hasFuelGauge && fuelGaugeRuntime.ready && Wire.sealed);
    assert(pinLevels[CHRG_CTRL] == HIGH && !fuelGaugeReading().valid);
    const int operations = Wire.operations;
    assert(fuelGaugePollDetection() && Wire.operations == operations);
  }
}

static void checkProbeIdentity() {
  for (const auto &results : {std::vector<uint8_t>{2, 2, 5, 2, 2},
                              std::vector<uint8_t>{5, 2, 2, 2, 2},
                              std::vector<uint8_t>{2, 2, 2, 2, 4}}) {
    reset();
    Wire.probeResults = results;
    fuelGaugeStartDetection(700, false);
    for (int attempt = 0; attempt < 5; ++attempt) {
      if (attempt > 0) delay(200);
      assert(fuelGaugePollDetection() == (attempt == 4));
      assert(!fuelGaugeLegacyHardware() && !b_hasFuelGauge && pinWrites.empty());
    }
    assert(fuelGaugeRuntime.hardware == BatteryHardware::Unknown);
  }

  reset();
  Wire.deviceType = 0;
  fuelGaugeStartDetection(700, false);
  assert(fuelGaugePollDetection() && b_hasFuelGauge && !fuelGaugeRuntime.ready);
  assert(!fuelGaugeLegacyHardware() && pinLevels[CHRG_CTRL] == HIGH);
  assert(!fuelGaugeReading().valid);
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt > 0) delay(200);
    assert(fuelGaugePollDetection() == (attempt == 4));
    assert(b_hasFuelGauge && !fuelGaugeLegacyHardware());
  }
  assert(!fuelGaugeRuntime.ready && fuelGaugeRuntime.hardware == BatteryHardware::Gauge);
  assert(std::find(pinWrites.begin(), pinWrites.end(), GPOUT) == pinWrites.end());
}

static void checkSampling() {
  reset();
  assert(!fuelGaugeLegacyHardware());
  assert(fuelGaugeBegin(700, false));
  assert(b_hasFuelGauge && !fuelGaugeLegacyHardware());
  assert(pinModes[14] == INPUT && pinLevels[6] == HIGH);
  assert(std::find(pinWrites.begin(), pinWrites.end(), 14) == pinWrites.end());
  assert(Wire.sealed && Wire.configEntries == 0 && Wire.resets == 0);
  sample(74);
  assert(fuelGaugeReading().valid && pinLevels[6] == LOW);
  const int operations = Wire.operations;
  for (int index = 0; index < 100; ++index) {
    fuelGaugeLoop();
    assert(fuelGaugeSocPercent() == 74);
    assert(fuelGaugeVoltageV() == 3.8f);
    assert(fuelGaugeReading().valid);
  }
  assert(Wire.operations == operations);
  sample(80);
  assert(pinLevels[6] == HIGH);
  sample(76);
  assert(pinLevels[6] == HIGH);
  sample(75);
  assert(pinLevels[6] == LOW);
  const uint32_t sequence = fuelGaugeReading().sequence;
  Wire.shortReg = 0x04;
  sample(80);
  assert(!fuelGaugeReading().valid && pinLevels[6] == HIGH);
  assert(fuelGaugeReading().sequence == sequence);
  fuelGaugeProtectSet(false);
  assert(pinLevels[6] == HIGH);
  Wire.shortReg = -1;
  sample(80);
  assert(fuelGaugeReading().valid && pinLevels[6] == LOW);
  Wire.negativeReg = 0x1c;
  sample(50);
  assert(!fuelGaugeReading().valid && pinLevels[6] == HIGH);
  Wire.negativeReg = -1;
  sample(255);
  assert(!fuelGaugeReading().valid && pinLevels[6] == HIGH);
  Wire.words[0x04] = 65535;
  sample(50);
  assert(!fuelGaugeReading().valid);
  Wire.words[0x04] = 3100;
  sample(0);
  assert(fuelGaugeReading().valid && fuelGaugeSocPercent() == 0);
  delay(2500);
  assert(!fuelGaugeReading().valid);
  fuelGaugeProtectSet(false);
  assert(pinLevels[6] == HIGH);
  for (uint8_t reg : {0x04, 0x1c, 0x1e, 0x10, 0x18, 0x0c, 0x0e, 0x20}) {
    const auto &times = Wire.reads[reg];
    for (size_t index = 1; index < times.size(); ++index) assert(times[index] - times[index - 1] >= 1000);
  }
}

static void checkIdentityAndSleep() {
  reset();
  Wire.present = false;
  assert(!fuelGaugeBegin(700, false));
  assert(!b_hasFuelGauge && fuelGaugeLegacyHardware());
  fuelGaugeProtectSet(false);
  fuelGaugeSleep();
  assert(pinWrites.empty());
  reset();
  Wire.present = false;
  Wire.errorCode = 5;
  assert(!fuelGaugeBegin(700, false));
  assert(!b_hasFuelGauge && !fuelGaugeLegacyHardware() && pinWrites.empty());
  reset();
  Wire.chemistry = 0x1210;
  Wire.configTimeout = true;
  assert(!fuelGaugeBegin(700, false));
  assert(b_hasFuelGauge && !fuelGaugeLegacyHardware() && Wire.sealed);
  assert(pinLevels[6] == HIGH && !fuelGaugeReading().valid);
  Wire.configTimeout = false;
  delay(10000);
  fuelGaugeLoop();
  assert(fuelGaugeRuntime.ready);
  sample(50);
  assert(pinLevels[6] == LOW);
  fuelGaugeSleep();
  assert(pinLevels[6] == HIGH && pinHolds[6]);
  assert(!pinHolds[14]);
  assert(fuelGaugeBegin(700, false));
  assert(!pinHolds[6] && pinLevels[6] == HIGH);
  sample(50);
  fuelGaugeProtectSet(false);
  fuelGaugeSleep();
  assert(pinHolds[6] && pinLevels[6] == HIGH);
  fuelGaugePauseCharging();
  assert(pinLevels[6] == HIGH);
}

static void checkConfiguration() {
  reset();
  Wire.chemistry = 0x1210;
  Wire.blocks[105][5] = 0x81;
  Wire.words[0x06] |= 0x20;
  assert(fuelGaugeBegin(1100, false));
  assert(Wire.chemistry == 0x1202 && Wire.capacity() == 1100);
  assert(Wire.blocks[105][5] == 1 && Wire.sealed && !(Wire.words[0x06] & 0x30));
  assert(Wire.configEntries == 1 && Wire.resets == 1);
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  sample(40);
  assert(Wire.capacity() == 1100 && Wire.sealed);
  assert(!fuelGaugeReading().valid && pinLevels[6] == HIGH);
  sample(40);
  assert(fuelGaugeReading().valid);
  assert(fuelGaugeSetCapacity(900));
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  sample(40);
  assert(Wire.capacity() == 900);
  assert(!fuelGaugeSetCapacity(299) && !fuelGaugeSetCapacity(2001));
  reset();
  Wire.setCapacity(1000);
  assert(fuelGaugeBegin(0, true));
  assert(Wire.capacity() == 1000 && fuelGaugeRuntime.desiredCapacity == 1000);
  assert(fuelGaugeRuntime.capacityPersistPending);
  reset();
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  assert(fuelGaugeBegin(0, true));
  assert(Wire.capacity() == 700);
  reset();
  Wire.sealed = false;
  uint16_t capacity = 850;
  assert(gaugeConfigure(capacity, false));
  assert(!Wire.sealed && Wire.seals == 0);
  reset();
  Wire.checksumBad = true;
  assert(!fuelGaugeBegin(700, false));
  assert(Wire.sealed && pinLevels[6] == HIGH);
}

static void checkTransactionFailures() {
  reset();
  assert(fuelGaugeBegin(900, false));
  const int operations = Wire.operations;
  for (int failure = 1; failure <= operations; ++failure) {
    reset();
    Wire.failAt = failure;
    assert(!fuelGaugeBegin(900, false) || fuelGaugeRuntime.ready);
    assert(b_hasFuelGauge && !fuelGaugeLegacyHardware());
    assert(pinLevels[6] == HIGH && !fuelGaugeReading().valid);
    if (!Wire.sealed) assert(!fuelGaugeRuntime.ready);
    delay(10000);
    fuelGaugeLoop();
    assert(fuelGaugeRuntime.ready && Wire.sealed && !(Wire.words[0x06] & 0x30));
  }
  uint16_t parsed = 0;
  for (const char *input : {"300", "700", "2000"}) assert(fuelGaugeParseCapacity(input, parsed));
  for (const char *input : {"", "299", "2001", "-700", "700x", "700 1", "66236", "999999999999999999999"}) {
    assert(!fuelGaugeParseCapacity(input, parsed));
  }
}

static void checkInfoScreen() {
  reset();
  assert(fuelGaugeBegin(700, false));
  sample(50);
  pinLevels[1] = HIGH;
  pinLevels[2] = HIGH;
  Wire.words[0x1c] = 80;
  buttonBackAt = millis() + 3500;
  showBatInfo();
  assert(menuServices > 2 && u8g2.frames > 2 && pinLevels[6] == HIGH);
  assert(fuelGaugeSocPercent() == 80);
  fuelGaugePauseCharging();
  const int operations = Wire.operations;
  drawBatInfoPage(0);
  assert(Wire.operations == operations && u8g2.unavailable);
}

static void checkResealRecovery() {
  reset();
  Wire.chemistry = 0x1210;
  Wire.words[0x06] |= 0x20;
  Wire.failCommand = 0x0020;
  assert(!fuelGaugeBegin(700, false));
  assert(!Wire.sealed && fuelGaugeRuntime.sealRequired && pinLevels[CHRG_CTRL] == HIGH);
  delay(10000);
  fuelGaugeLoop();
  assert(Wire.sealed && fuelGaugeRuntime.ready && !fuelGaugeRuntime.sealRequired);
  assert(pinLevels[CHRG_CTRL] == HIGH);
  sample(30);
  assert(pinLevels[CHRG_CTRL] == LOW);
}

int main() {
  checkProbeScheduling();
  checkDelayedGaugeDetection();
  checkProbeIdentity();
  checkSampling();
  checkIdentityAndSleep();
  checkConfiguration();
  checkTransactionFailures();
  checkInfoScreen();
  checkResealRecovery();
}
