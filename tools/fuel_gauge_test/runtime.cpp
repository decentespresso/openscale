#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include "config.h"
#include <Wire.h>
#include "driver/gpio.h"
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
FuelGaugeSleepState fuelGaugeSleepState{};
constexpr uint32_t WOW_RTC_MAGIC = 0x574f5733;
struct { uint32_t magic = 0; bool armed = false; uint32_t intervalUs = 0; } wowRtc;
#define BUTTON_PIN_BITMASK(pin) (1ULL << (pin))
constexpr uint64_t PIN_BITMASK = (1ULL << BUTTON_CIRCLE) | (1ULL << BUTTON_SQUARE) | (1ULL << BATTERY_CHARGING);
void configureWakePinForDeepSleep(gpio_num_t) {}
void configureWakePinsForDeepSleep() {}
int rtc_gpio_get_level(gpio_num_t pin) { return digitalRead(pin); }
#include "fuel_gauge_sleep.h"

static void reset() {
  fakeMicros = 0;
  Wire = FakeWire{};
  fuelGaugeRuntime = FuelGaugeRuntime{};
  fuelGaugeSleepState = {};
  wowRtc = {};
  b_hasFuelGauge = false;
  b_batteryProtect = true;
  pinWrites.clear();
  pinLevels.fill(0);
  pinModes.fill(0);
  pinHolds.fill(false);
  buttonBackAt = std::numeric_limits<unsigned long>::max();
  fakeWakeCause = ESP_SLEEP_WAKEUP_TIMER;
  fakeWakeStatus = 0;
  fakeWakeMask = 0;
  fakeTimerUs = 0;
}

static void sample(uint16_t soc) {
  Wire.words[0x1c] = soc;
  delay(1000);
  fuelGaugeLoop();
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
  assert(pinLevels[6] == LOW && pinHolds[6] && fuelGaugeDeepSleepActive());
  assert(!pinHolds[14]);
  assert(fuelGaugeBegin(700, false));
  assert(!pinHolds[6] && pinLevels[6] == HIGH);
  sample(50);
  fuelGaugeProtectSet(false);
  fuelGaugeSleep();
  assert(pinHolds[6] && pinLevels[6] == LOW);
  assert(!fuelGaugeDeepSleepActive());
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

static void startProtectedSleep(uint16_t soc) {
  reset();
  assert(fuelGaugeBegin(700, false));
  sample(soc);
  fuelGaugeSleep();
  assert(fuelGaugeDeepSleepActive());
  pinLevels[BUTTON_CIRCLE] = HIGH;
  pinLevels[BUTTON_SQUARE] = HIGH;
  pinLevels[FUEL_GAUGE_USB_DET] = LOW;
}

static void microBoot() {
  fuelGaugeRuntime = {};
  b_hasFuelGauge = false;
  b_batteryProtect = false;
  fakeMicros = 0;
  pinLevels[CHRG_CTRL] = LOW;
}

static bool slept() {
  try { fuelGaugeMicroWakeOrContinue(); }
  catch (const FakeDeepSleep &) { return true; }
  return false;
}

static void checkProtectedSleep() {
  reset();
  assert(fuelGaugeMicroWakeOrContinue() == -1 && Wire.operations == 0 && pinWrites.empty());
  startProtectedSleep(30);
  assert(pinLevels[CHRG_CTRL] == LOW);
  microBoot();
  const int writes = Serial.writes;
  pinWrites.clear();
  assert(slept());
  for (int pin : pinWrites) assert(pin == CHRG_CTRL || pin == I2C_SDA || pin == I2C_SCL);
  assert(pinLevels[CHRG_CTRL] == LOW && pinHolds[CHRG_CTRL]);
  assert(fakeTimerUs == 10000000);
  assert(fakeWakeMask == ((1ULL << BUTTON_CIRCLE) | (1ULL << BUTTON_SQUARE)));
  assert(Serial.writes == writes);
  assert(pinHolds[I2C_SDA] && pinHolds[I2C_SCL]);
  for (uint16_t soc : {76, 80, 78, 75}) {
    Wire.words[0x1c] = soc;
    microBoot();
    assert(slept());
    assert(pinLevels[CHRG_CTRL] == (soc >= 78 ? HIGH : LOW));
  }
  Wire.shortReg = 0x04;
  microBoot();
  assert(slept() && pinLevels[CHRG_CTRL] == HIGH);
  Wire.shortReg = -1;
  Wire.words[0x1c] = 30;
  microBoot();
  assert(slept() && pinLevels[CHRG_CTRL] == LOW);
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  microBoot();
  assert(slept() && Wire.capacity() == 700 && pinLevels[CHRG_CTRL] == HIGH);
  microBoot();
  assert(slept() && pinLevels[CHRG_CTRL] == LOW);
  pinLevels[FUEL_GAUGE_USB_DET] = HIGH;
  microBoot();
  assert(slept() && fakeTimerUs == 0);
  assert(fakeWakeMask & (1ULL << FUEL_GAUGE_USB_DET));
  fakeWakeCause = ESP_SLEEP_WAKEUP_EXT1;
  fakeWakeStatus = 1ULL << FUEL_GAUGE_USB_DET;
  pinLevels[FUEL_GAUGE_USB_DET] = LOW;
  microBoot();
  assert(slept() && fakeTimerUs == 10000000);
  fakeWakeStatus = 1ULL << BUTTON_SQUARE;
  microBoot();
  assert(fuelGaugeMicroWakeOrContinue() == -1 && pinLevels[CHRG_CTRL] == HIGH);
  fakeWakeCause = ESP_SLEEP_WAKEUP_TIMER;
  pinLevels[BUTTON_CIRCLE] = LOW;
  microBoot();
  assert(fuelGaugeMicroWakeOrContinue() == BUTTON_CIRCLE);
  assert(pinLevels[CHRG_CTRL] == HIGH);
  pinLevels[BUTTON_CIRCLE] = HIGH;
  wowRtc = {WOW_RTC_MAGIC, true, 2000000};
  microBoot();
  assert(fuelGaugeMicroWakeOrContinue() == -1 && fakeTimerUs == 2000000);
  assert(fuelGaugeDeepSleepActive());
  fakeWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
  assert(fuelGaugeMicroWakeOrContinue() == -1 && !fuelGaugeDeepSleepActive());
  startProtectedSleep(30);
  microBoot();
  Wire.present = false;
  assert(!fuelGaugeBegin(700, false));
  assert(b_hasFuelGauge && !fuelGaugeLegacyHardware() && pinLevels[CHRG_CTRL] == HIGH);
  assert(!fuelGaugeRuntime.ready);
  startProtectedSleep(30);
  Wire.chemistry = 0x1210;
  Wire.words[0x06] |= 0x20;
  Wire.failCommand = 0x0020;
  microBoot();
  assert(slept() && !Wire.sealed && !fuelGaugeSleepState.ready);
  assert(fuelGaugeSleepState.sealRequired && pinLevels[CHRG_CTRL] == HIGH);
  microBoot();
  assert(slept() && Wire.sealed && fuelGaugeSleepState.ready);
  assert(!fuelGaugeSleepState.sealRequired && pinLevels[CHRG_CTRL] == HIGH);
  microBoot();
  assert(slept() && pinLevels[CHRG_CTRL] == LOW);
  microBoot();
  buttonBackAt = 1;
  assert(fuelGaugeMicroWakeOrContinue() == BUTTON_CIRCLE);
  assert(pinLevels[CHRG_CTRL] == HIGH);
}

int main() {
  checkSampling();
  checkIdentityAndSleep();
  checkConfiguration();
  checkTransactionFailures();
  checkInfoScreen();
  checkProtectedSleep();
}
