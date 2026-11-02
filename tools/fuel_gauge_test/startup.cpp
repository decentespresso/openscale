#include "fuel_gauge.h"
#include "config.h"
#include <Wire.h>
#include <algorithm>

volatile bool b_hasFuelGauge = false;
volatile bool b_batteryProtect = true;
FuelGaugeRuntime fuelGaugeRuntime;
bool b_u8g2Sleep = true;
bool b_screenFlipped = false;
bool storedScreenFlip = false;
constexpr const char *KEY_SCREEN_FLIP = "screen_flip";
constexpr int FONT_M = 1, U8G2_R0 = 0, U8G2_R2 = 2;
constexpr int u8g2_font_logisoso22_tf = 2;
constexpr int LCDWidth = 128, LCDHeight = 64;
#define AC(text) 0

bool storageGetBool(const char *, bool) { return storedScreenFlip; }
void invalidateEnergyOledFrame() {}
void yield() { delay(1); }

struct FakeOled {
  int frames = 0, rotation = -1, operations = 0;
  bool initialized = false;
  void begin() { initialized = true; }
  void enableUTF8Print() {}
  void setContrast(int) {}
  void setFont(int) {}
  void setDisplayRotation(int value) { rotation = value; }
  void firstPage() { assert(initialized); ++frames; operations = Wire.operations; }
  void drawStr(int, int, const char *) {}
  void drawBox(int, int, int, int) {}
  bool nextPage() { assert(operations == Wire.operations); return false; }
} u8g2;

struct FakeScale {
  std::vector<unsigned long> updateTimes;
  void update() { updateTimes.push_back(millis()); }
} scale;

#include "boot_startup.h"
static_assert(HDS_OVERLAP_GAUGE_STARTUP == 1);

static void reset() {
  fakeMicros = 0;
  Wire = FakeWire{};
  fuelGaugeRuntime = FuelGaugeRuntime{};
  b_hasFuelGauge = false;
  b_batteryProtect = true;
  b_u8g2Sleep = true;
  storedScreenFlip = false;
  u8g2 = FakeOled{};
  scale = FakeScale{};
  pinWrites.clear();
  pinLevels.fill(0);
  pinModes.fill(0);
  pinHolds.fill(false);
}

static void checkLogo() {
  reset();
  initializeBootDisplay();
  renderBootLogo();
  assert(!b_u8g2Sleep && u8g2.frames == 1 && u8g2.rotation == U8G2_R2);
  assert(Wire.operations == 0 && pinWrites.empty());
  storedScreenFlip = true;
  renderBootLogo();
  assert(u8g2.frames == 2 && u8g2.rotation == U8G2_R0);
}

static void checkAbsentGauge(bool busFault) {
  reset();
  Wire.present = false;
  if (busFault) Wire.probeResults = {2, 5, 2, 2, 2};
  fuelGaugeStartDetection(700, false);
  stabilizeScaleWithFuelGauge();
  assert(millis() == 900 && Wire.probeTimes.size() == 5);
  assert(fuelGaugeLegacyHardware() == !busFault);
  assert(!b_hasFuelGauge && pinWrites.empty());
  assert(scale.updateTimes.size() >= 899);
  assert(scale.updateTimes.front() == 0 && scale.updateTimes.back() == 899);
  for (size_t index = 1; index < scale.updateTimes.size(); ++index) {
    assert(scale.updateTimes[index] - scale.updateTimes[index - 1] <= 1);
  }
}

static void checkAcknowledgedGauge(size_t attempt, bool configFails) {
  reset();
  Wire.probeResults = std::vector<uint8_t>(attempt - 1, 2);
  Wire.probeResults.push_back(0);
  if (configFails) Wire.deviceType = 0;
  fuelGaugeStartDetection(700, false);
  stabilizeScaleWithFuelGauge();
  assert(Wire.probeTimes.size() == attempt);
  assert(b_hasFuelGauge && !fuelGaugeLegacyHardware());
  assert(fuelGaugeRuntime.ready == !configFails);
  assert(millis() - fuelGaugeRuntime.lastRecovery == 900);
  assert(scale.updateTimes.back() - fuelGaugeRuntime.lastRecovery == 899);
  assert(pinLevels[6] == HIGH && pinModes[14] == INPUT);
  assert(std::find(pinWrites.begin(), pinWrites.end(), 14) == pinWrites.end());
}

static void checkSlowBus() {
  reset();
  Wire.present = false;
  Wire.probeDelayMs = 100;
  fuelGaugeStartDetection(700, false);
  stabilizeScaleWithFuelGauge();
  assert(Wire.probeTimes.size() == 5 && fuelGaugeLegacyHardware());
  assert(millis() >= 1300 && millis() <= 1301);
}

int main() {
  checkLogo();
  checkAbsentGauge(false);
  checkAbsentGauge(true);
  for (size_t attempt = 1; attempt <= 5; ++attempt) {
    checkAcknowledgedGauge(attempt, false);
    checkAcknowledgedGauge(attempt, true);
  }
  checkSlowBus();
}
