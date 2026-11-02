#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include "config.h"
#include <Wire.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

volatile bool b_hasFuelGauge = false;
volatile bool b_batteryProtect = true;
volatile bool b_ota = false;
FuelGaugeRuntime fuelGaugeRuntime;
bool persistedReseal = false;
int storeCalls = 0, failStoreValue = -1, failStoreAt = -1;
uint16_t persistedCapacity = 0;
int capacityStoreCalls = 0;
std::vector<uint16_t> capacityStoreFlags;
bool capacityStoreSucceeds = true, capacityReadbackSucceeds = true;
const char *activeCheck = "startup";
int activeCase = 0;

bool fuelGaugeStoreReseal(bool required) {
  ++storeCalls;
  if (required == failStoreValue || storeCalls == failStoreAt) return false;
  if (!required) assert(Wire.sealed && !(Wire.words[0x06] & 0x30));
  persistedReseal = required;
  return true;
}

bool fuelGaugeStoreCapacity(uint16_t capacity) {
  ++capacityStoreCalls;
  capacityStoreFlags.push_back(Wire.words[0x06]);
  if (!capacityStoreSucceeds) return false;
  persistedCapacity = capacity;
  return capacityReadbackSucceeds;
}

static void service() {
  const int operations = Wire.operations;
  const auto transactions = Wire.transactions.size();
  const unsigned long delays = millisecondDelayCalls;
  const uint64_t startedAt = fakeMicros;
  fuelGaugeLoop();
  assert(Wire.operations - operations <= 1);
  assert(Wire.transactions.size() - transactions <= 1);
  assert(millisecondDelayCalls == delays);
  assert(fakeMicros - startedAt <= 70);
}

static void advance(unsigned long duration = 10) {
  fakeMicros += static_cast<uint64_t>(duration) * 1000;
}

template<class Predicate>
static void until(Predicate done, unsigned long timeout = 60000) {
  const unsigned long startedAt = millis();
  while (!done()) {
    if (millis() - startedAt > timeout) {
      std::fprintf(stderr, "%s case %d timed out: stage=%d io=%d phase=%u ready=%d flags=%u sealed=%d capacity=%u desired=%u\n",
                   activeCheck, activeCase, static_cast<int>(fuelGaugeRuntime.engine.stage),
                   static_cast<int>(fuelGaugeRuntime.engine.io.kind), fuelGaugeRuntime.engine.io.phase,
                   fuelGaugeRuntime.ready, Wire.words[0x06], Wire.sealed, Wire.capacity(), fuelGaugeRuntime.desiredCapacity);
    }
    assert(millis() - startedAt <= timeout);
    service();
    advance();
  }
}

#define POWER_H
#define DISPLAY_H
#define AR(text) 100
constexpr int u8g2_font_6x12_tr = 0;
float f_batteryVoltage = 3.8f;
int menuServices = 0;
int batteryPercent() { return fuelGaugeSocPercent(); }
void serviceFuelGauge() { ++menuServices; service(); }
void power_off(int) { serviceFuelGauge(); }
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

static void reset(bool retainPersistence = false) {
  fakeMicros = 0;
  millisecondDelayCalls = 0;
  Wire = FakeWire{};
  fuelGaugeRuntime = FuelGaugeRuntime{};
  b_hasFuelGauge = false;
  b_batteryProtect = true;
  b_ota = false;
  pinWrites.clear();
  pinLevels.fill(0);
  pinModes.fill(-1);
  pinHolds.fill(false);
  buttonBackAt = std::numeric_limits<unsigned long>::max();
  if (!retainPersistence) {
    persistedReseal = false;
    persistedCapacity = 0;
  }
  storeCalls = 0;
  capacityStoreCalls = 0;
  capacityStoreFlags.clear();
  capacityStoreSucceeds = true;
  capacityReadbackSucceeds = true;
  failStoreValue = -1;
  failStoreAt = -1;
  fuelGaugeSetResealPending(persistedReseal);
}

static void begin(uint16_t capacity = 700, bool adopt = false) {
  fuelGaugeStartDetection(capacity, adopt);
  until([] { return fuelGaugeRuntime.probeComplete; });
  assert(b_hasFuelGauge && !fuelGaugeRuntime.ready);
  assert(Wire.commands.empty() && storeCalls == 0);
  fuelGaugeAllowConfiguration();
}

static void ready() {
  until([] { return fuelGaugeRuntime.ready; });
  assert(pinLevels[CHRG_CTRL] == HIGH && !fuelGaugeReading().valid);
  assert(!persistedReseal);
}

static void sample(uint16_t soc) {
  Wire.words[0x1c] = soc;
  const uint32_t sequence = fuelGaugeRuntime.reading.sequence;
  until([sequence] { return fuelGaugeRuntime.reading.sequence != sequence; });
  assert(fuelGaugeReading().valid);
}

static void dirtyConfiguration() {
  Wire.chemistry = 0x1210;
  Wire.blocks[105][5] = 0x81;
  Wire.words[0x06] |= 0x20;
}

static void checkProbeScheduling() {
  reset();
  assert(fuelGaugePollDetection() && Wire.operations == 0);
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  assert(Wire.timeoutMs > 0 && Wire.timeoutMs <= 100);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (attempt) {
      advance(199);
      assert(!fuelGaugePollDetection());
      assert(Wire.operations == attempt);
      advance(1);
    }
    assert(fuelGaugePollDetection() == (attempt == 4));
    assert(Wire.operations == attempt + 1 && pinWrites.empty());
  }
  assert(fuelGaugeLegacyHardware());
  reset();
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  service();
  advance(5000);
  service();
  const int operations = Wire.operations;
  for (int index = 0; index < 100; ++index) service();
  assert(Wire.operations == operations);
  advance(199);
  service();
  assert(Wire.operations == operations);
  advance(1);
  service();
  assert(Wire.operations == operations + 1);
  reset();
  Wire.present = false;
  fakeMicros = static_cast<uint64_t>(std::numeric_limits<unsigned long>::max() - 100) * 1000;
  fuelGaugeStartDetection(700, false);
  service();
  advance(199);
  service();
  assert(Wire.operations == 1);
  advance(1);
  service();
  assert(Wire.operations == 2);
}

static void checkDiscoveryAndAcceptance() {
  for (int acknowledgedAttempt = 1; acknowledgedAttempt <= 5; ++acknowledgedAttempt) {
    reset();
    Wire.probeResults = std::vector<uint8_t>(acknowledgedAttempt - 1, 2);
    Wire.probeResults.push_back(0);
    fuelGaugeStartDetection(1100, false);
    until([] { return fuelGaugeRuntime.probeComplete; });
    assert(Wire.probeTimes.size() == static_cast<size_t>(acknowledgedAttempt));
    assert(b_hasFuelGauge && !fuelGaugeLegacyHardware() && !fuelGaugeRuntime.ready);
    assert(pinModes[GPOUT] == INPUT && pinLevels[CHRG_CTRL] == HIGH);
    assert(std::find(pinWrites.begin(), pinWrites.end(), GPOUT) == pinWrites.end());
    for (int index = 0; index < 2000; ++index) { service(); advance(); }
    assert(Wire.commands.empty() && storeCalls == 0 && Wire.sealed);
    fuelGaugeAllowConfiguration();
    ready();
    assert(Wire.capacity() == 1100);
    Wire.present = false;
    fuelGaugeStartDetection(700, false);
    until([] { return fuelGaugeRuntime.probeComplete; });
    assert(b_hasFuelGauge && fuelGaugeRuntime.hardware == BatteryHardware::Gauge);
    assert(!fuelGaugeLegacyHardware() && !fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
  }
  for (const auto &results : {std::vector<uint8_t>{2, 2, 5, 2, 2},
                              std::vector<uint8_t>{5, 2, 2, 2, 2}}) {
    reset();
    Wire.probeResults = results;
    fuelGaugeStartDetection(700, false);
    until([] { return fuelGaugeRuntime.probeComplete; });
    assert(!fuelGaugeLegacyHardware() && !b_hasFuelGauge && pinWrites.empty());
  }
  reset();
  persistedReseal = true;
  fuelGaugeSetResealPending(true);
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  until([] { return fuelGaugeRuntime.probeComplete; });
  assert(fuelGaugeRuntime.hardware == BatteryHardware::Unknown);
  assert(!fuelGaugeLegacyHardware() && pinWrites.empty());
}

static void checkConfiguration() {
  reset();
  dirtyConfiguration();
  begin(1100);
  ready();
  assert(Wire.capacity() == 1100 && Wire.chemistry == 0x1202);
  assert(Wire.blocks[105][5] == 1 && Wire.sealed && !(Wire.words[0x06] & 0x30));
  assert(Wire.configEntries == 1 && Wire.resets == 1 && storeCalls >= 2);
  sample(50);
  assert(pinLevels[CHRG_CTRL] == LOW);
  Wire.words[0x06] |= 0x20;
  Wire.setCapacity(1340);
  until([] { return !fuelGaugeRuntime.ready; });
  assert(pinLevels[CHRG_CTRL] == HIGH && !fuelGaugeReading().valid);
  ready();
  assert(Wire.capacity() == 1100);
  reset();
  Wire.setCapacity(1000);
  begin(0, true);
  ready();
  assert(fuelGaugeRuntime.desiredCapacity == 1000 && fuelGaugeRuntime.capacityPersistPending);
  reset();
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  begin(0, true);
  ready();
  assert(Wire.capacity() == 700);
  for (bool por : {false, true}) {
    reset();
    persistedReseal = true;
    fuelGaugeSetResealPending(true);
    Wire.setCapacity(por ? 1340 : 1000);
    if (por) Wire.words[0x06] |= 0x20;
    begin(0, true);
    ready();
    assert(Wire.capacity() == (por ? 700 : 1000));
    assert(fuelGaugeRuntime.desiredCapacity == Wire.capacity());
    assert(fuelGaugeRuntime.capacityPersistPending);
  }
  reset();
  Wire.sealed = false;
  begin(850);
  ready();
  assert(!Wire.sealed && Wire.seals == 0 && storeCalls == 0);
}

static void checkSamples() {
  reset();
  begin();
  ready();
  for (const auto &entry : {std::pair<uint16_t, int>{74, LOW}, {80, HIGH}, {76, HIGH}, {75, LOW}}) {
    sample(entry.first);
    assert(pinLevels[CHRG_CTRL] == entry.second);
  }
  sample(0);
  assert(fuelGaugeSocPercent() == 0 && fuelGaugeReading().valid);
  const uint32_t sequence = fuelGaugeReading().sequence;
  const FuelGaugeReading previous = fuelGaugeReading();
  Wire.words[0x04] = 4100;
  Wire.shortReg = 0x1c;
  for (int index = 0; index < 500; ++index) { service(); advance(); }
  assert(!fuelGaugeReading().valid && pinLevels[CHRG_CTRL] == HIGH);
  assert(fuelGaugeRuntime.reading.sequence == sequence);
  assert(fuelGaugeRuntime.reading.voltageMv == previous.voltageMv);
  Wire.shortReg = -1;
  until([] { return fuelGaugeRuntime.ready; });
  sample(80);
  fuelGaugeProtectSet(false);
  assert(pinLevels[CHRG_CTRL] == LOW);
  advance(2500);
  assert(!fuelGaugeReading().valid);
  fuelGaugeProtectSet(false);
  assert(pinLevels[CHRG_CTRL] == HIGH);
  for (const auto &invalid : {std::pair<uint8_t, uint16_t>{0x1c, 101}, {0x04, 0},
                              {0x04, 5001}, {0x1e, 1730}, {0x1e, 4733}, {0x20, 101}}) {
    reset();
    begin();
    ready();
    Wire.words[invalid.first] = invalid.second;
    for (int index = 0; index < 500; ++index) { service(); advance(); }
    assert(!fuelGaugeReading().valid && pinLevels[CHRG_CTRL] == HIGH);
  }
  reset();
  begin();
  ready();
  sample(50);
  const FuelGaugeReading beforeSlowSample = fuelGaugeReading();
  Wire.words[0x04] = 4200;
  until([] { return fuelGaugeRuntime.engine.stage == GaugeStage::Sample; });
  while (fuelGaugeRuntime.engine.stage == GaugeStage::Sample) {
    advance(1000);
    service();
    assert(fuelGaugeRuntime.reading.sequence == beforeSlowSample.sequence);
    assert(fuelGaugeRuntime.reading.voltageMv == beforeSlowSample.voltageMv);
    if (millis() - beforeSlowSample.sampledAt >= 2500) {
      assert(!fuelGaugeReading().valid && pinLevels[CHRG_CTRL] == HIGH);
    }
  }
  assert(!fuelGaugeRuntime.ready);
}

static void checkQueuedCapacity() {
  reset();
  dirtyConfiguration();
  begin(1100);
  until([] { return !Wire.sealed; });
  assert(fuelGaugeBusy());
  assert(!fuelGaugeSetCapacity(900));
  assert(fuelGaugeRuntime.desiredCapacity == 900);
  until([] { return Wire.capacity() == 1100; });
  assert(!fuelGaugeRuntime.ready || fuelGaugeBusy());
  until([] { return fuelGaugeRuntime.ready && Wire.capacity() == 900; });
  assert(!fuelGaugeSetCapacity(299) && !fuelGaugeSetCapacity(2001));
  assert(fuelGaugeSetCapacity(1000));
  assert(Wire.capacity() == 1000 && fuelGaugeRuntime.ready);
  uint16_t parsed = 0;
  for (const char *input : {"300", "700", "2000"}) assert(fuelGaugeParseCapacity(input, parsed));
  for (const char *input : {"", "299", "2001", "-700", "700x", "700 1", "66236", "999999999999999999999"}) {
    assert(!fuelGaugeParseCapacity(input, parsed));
  }
}

static void checkOperationTiming() {
  reset();
  dirtyConfiguration();
  begin(900);
  until([] { return fuelGaugeRuntime.engine.stage == GaugeStage::EnterSettle; });
  const int operations = Wire.operations;
  for (int index = 0; index < 1000; ++index) service();
  assert(Wire.operations == operations);
  assert(fuelGaugeRuntime.engine.stage == GaugeStage::EnterSettle);
  advance(10000);
  service();
  service();
  for (int index = 0; index < 1000; ++index) service();
  assert(Wire.operations == operations);
  ready();
  sample(50);
  Wire.shortReg = 0x04;
  until([] { return !fuelGaugeRuntime.ready; });
  Wire.shortReg = -1;
  const int failedAt = Wire.operations;
  const unsigned long recoveryAt = fuelGaugeRuntime.lastRecovery;
  while (millis() - recoveryAt < 9990) { service(); advance(); }
  assert(Wire.operations == failedAt);
  ready();
  reset();
  fakeMicros = static_cast<uint64_t>(std::numeric_limits<unsigned long>::max() - 1000) * 1000;
  dirtyConfiguration();
  begin(900);
  ready();
  assert(millis() < 60000 && Wire.sealed && Wire.capacity() == 900);
}

static void checkIdentityAndInvalidConfiguration() {
  for (bool pending : {false, true}) {
    for (bool por : {false, true}) {
      reset();
      persistedReseal = pending;
      fuelGaugeSetResealPending(pending);
      Wire.deviceType = 0;
      if (por) Wire.words[0x06] |= 0x20;
      begin(900);
      for (int index = 0; index < 2500; ++index) { service(); advance(); }
      assert(b_hasFuelGauge && !fuelGaugeLegacyHardware() && !fuelGaugeRuntime.ready);
      for (uint16_t command : {0x8000, 0x0013, 0x0031, 0x0042, 0x0020}) {
        assert(std::find(Wire.commands.begin(), Wire.commands.end(), command) == Wire.commands.end());
      }
      assert(Wire.sealed && Wire.capacity() == 700 && pinLevels[CHRG_CTRL] == HIGH);
      fuelGaugeRequestQuiesce();
      until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
      assert(fuelGaugeQuiesceStatus() == (pending ? FuelGaugeQuiesceStatus::Failed : FuelGaugeQuiesceStatus::Complete));
      assert(persistedReseal == pending);
    }
  }
  for (bool badChecksum : {false, true}) {
    reset();
    dirtyConfiguration();
    Wire.checksumBad = badChecksum;
    Wire.configTimeout = !badChecksum;
    begin(900);
    for (int index = 0; index < 2500; ++index) { service(); advance(); }
    assert(!fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
    fuelGaugeRequestQuiesce();
    until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete && Wire.sealed);
    Wire.checksumBad = false;
    Wire.configTimeout = false;
    fuelGaugeResume();
    ready();
  }
}

static void checkPersistence() {
  reset();
  dirtyConfiguration();
  failStoreValue = true;
  begin(900);
  for (int index = 0; index < 2000; ++index) { service(); advance(); }
  assert(Wire.sealed && Wire.unsealKeys == 0 && Wire.configEntries == 0);
  assert(std::find(Wire.commands.begin(), Wire.commands.end(), 0x8000) == Wire.commands.end());
  assert(!fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
  failStoreValue = -1;
  ready();
  reset();
  dirtyConfiguration();
  failStoreValue = false;
  begin(900);
  until([] { return Wire.seals > 0; });
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed);
  assert(persistedReseal && !fuelGaugeRuntime.ready);
  const int writesAfterFailure = storeCalls;
  for (int index = 0; index < 1000; ++index) { service(); advance(); }
  assert(storeCalls == writesAfterFailure);
}

static int configurationTransactions() {
  reset();
  dirtyConfiguration();
  begin(900);
  ready();
  return Wire.operations;
}

static void checkTransactionFailures() {
  const int operations = configurationTransactions();
  for (int failed = 2; failed <= operations; ++failed) {
    activeCase = failed;
    reset();
    dirtyConfiguration();
    Wire.failAt = failed;
    begin(900);
    ready();
    assert(Wire.sealed && !(Wire.words[0x06] & 0x30));
    assert(Wire.capacity() == 900 && !persistedReseal);
  }
}

static void checkCancellationAndRestart() {
  const int operations = configurationTransactions();
  for (int cancelled = 2; cancelled <= operations; ++cancelled) {
    activeCase = cancelled;
    reset();
    dirtyConfiguration();
    begin(900);
    until([cancelled] { return Wire.operations >= cancelled; });
    const bool writingBlock = fuelGaugeRuntime.engine.io.kind == GaugeIoKind::WriteBlock &&
                              fuelGaugeRuntime.engine.io.phase <= 5;
    const int checksumWrites = Wire.checksumWrites;
    const GaugeIo &io = fuelGaugeRuntime.engine.io;
    const bool halfCommand = (io.kind == GaugeIoKind::Command || io.kind == GaugeIoKind::Control) && io.phase == 2;
    const uint16_t startedCommand = io.command;
    const auto completedCommands = std::count(Wire.commands.begin(), Wire.commands.end(), startedCommand);
    const unsigned long startedAt = millis();
    fuelGaugeRequestQuiesce();
    until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
    assert(millis() - startedAt <= 8000);
    assert(Wire.sealed && !(Wire.words[0x06] & 0x30) && !persistedReseal);
    if (writingBlock) assert(Wire.checksumWrites == checksumWrites + 1);
    if (halfCommand) assert(std::count(Wire.commands.begin(), Wire.commands.end(), startedCommand) > completedCommands);
    assert(!fuelGaugeReading().valid && pinLevels[CHRG_CTRL] == HIGH);
    const int idleOperations = Wire.operations;
    for (int index = 0; index < 10; ++index) { service(); advance(1000); }
    assert(Wire.operations == idleOperations);
    fuelGaugeResume();
    ready();
  }
  reset();
  dirtyConfiguration();
  begin(900);
  until([] { return !Wire.sealed && (Wire.words[0x06] & 0x10); });
  assert(persistedReseal);
  const FakeWire interrupted = Wire;
  reset(true);
  Wire = interrupted;
  const int unseals = static_cast<int>(std::count(Wire.commands.begin(), Wire.commands.end(), 0x8000));
  fuelGaugeStartDetection(700, false);
  until([] { return fuelGaugeRuntime.probeComplete; });
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
  assert(Wire.sealed && !persistedReseal && !(Wire.words[0x06] & 0x30));
  assert(std::count(Wire.commands.begin(), Wire.commands.end(), 0x8000) == unseals);
  reset();
  persistedReseal = true;
  fuelGaugeSetResealPending(true);
  dirtyConfiguration();
  fuelGaugeStartDetection(900, false);
  until([] { return fuelGaugeRuntime.probeComplete; });
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
  assert(Wire.sealed && !persistedReseal && !(Wire.words[0x06] & 0x30));
  assert(Wire.chemistry == 0x1210 && Wire.capacity() == 700 && Wire.checksumWrites == 0);
  assert(!fuelGaugeRuntime.configurationAllowed && !fuelGaugeRuntime.ready);
  reset();
  persistedReseal = true;
  fuelGaugeSetResealPending(true);
  Wire.present = false;
  fuelGaugeStartDetection(700, false);
  until([] { return fuelGaugeRuntime.probeComplete; });
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed);
  assert(fuelGaugeRuntime.hardware == BatteryHardware::Unknown && persistedReseal && pinWrites.empty());
}

static void checkBoundedCleanupAndMenu() {
  reset();
  dirtyConfiguration();
  begin(900);
  until([] { return (Wire.words[0x06] & 0x10) != 0; });
  const unsigned long startedAt = millis();
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
  assert(millis() - startedAt > 2000);
  reset();
  dirtyConfiguration();
  begin(900);
  until([] { return !Wire.sealed; });
  Wire.present = false;
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed && persistedReseal);
  fuelGaugeSleep();
  assert(pinHolds[CHRG_CTRL] && pinLevels[CHRG_CTRL] == HIGH && !pinHolds[GPOUT]);
  fuelGaugeResume();
  assert(!pinHolds[CHRG_CTRL] && pinLevels[CHRG_CTRL] == HIGH);
  reset();
  begin();
  ready();
  sample(50);
  pinLevels[1] = HIGH;
  pinLevels[2] = HIGH;
  Wire.words[0x1c] = 80;
  buttonBackAt = millis() + 3500;
  showBatInfo();
  assert(menuServices > 2 && u8g2.frames > 2 && pinLevels[CHRG_CTRL] == HIGH);
  assert(fuelGaugeSocPercent() == 80);
  fuelGaugePauseCharging();
  drawBatInfoPage(0);
  assert(u8g2.unavailable);
}

static void checkStuckCleanup() {
  for (bool exitStuck : {false, true}) {
    reset();
    dirtyConfiguration();
    begin(900);
    until([] { return (Wire.words[0x06] & 0x10) != 0; });
    Wire.exitIgnored = exitStuck;
    Wire.sealIgnored = !exitStuck;
    const unsigned long startedAt = millis();
    fuelGaugeRequestQuiesce();
    while (fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Pending) {
      fuelGaugeRequestQuiesce();
      service();
      advance();
      assert(millis() - startedAt <= 8020);
    }
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed);
    assert(millis() - startedAt >= 8000);
    assert(persistedReseal && !fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
    Wire.exitIgnored = false;
    Wire.sealIgnored = false;
    fuelGaugeResume();
    ready();
    assert(Wire.sealed && !(Wire.words[0x06] & 0x30) && !persistedReseal);
  }
}

static void checkResumePreservesCleanupDeadline() {
  for (bool pendingAtBoot : {false, true}) {
    reset();
    if (pendingAtBoot) {
      persistedReseal = true;
      fuelGaugeSetResealPending(true);
      Wire.sealed = false;
      Wire.words[0x06] |= 0x10;
      fuelGaugeStartDetection(900, false);
      until([] { return fuelGaugeRuntime.probeComplete; });
      until([] { return fuelGaugeRuntime.engine.cleanupStarted; });
    } else {
      dirtyConfiguration();
      begin(900);
      until([] { return (Wire.words[0x06] & 0x10) != 0; });
      fuelGaugeRequestQuiesce();
    }
    Wire.sealIgnored = true;
    const unsigned long startedAt = fuelGaugeRuntime.engine.cleanupStartedAt;
    while (millis() - startedAt < 3500) { service(); advance(); }
    assert(fuelGaugeBusy() && fuelGaugeRuntime.engine.cleanupStarted);
    fuelGaugeResume();
    assert(fuelGaugeRuntime.engine.cleanupStarted);
    assert(fuelGaugeRuntime.engine.cleanupStartedAt == startedAt);
    assert(fuelGaugeRuntime.engine.cleanupOnly);
    while (fuelGaugeBusy()) {
      service();
      advance();
      assert(millis() - startedAt <= 8020);
    }
    assert(millis() - startedAt >= 8000);
    assert(fuelGaugeRuntime.recoveryPending && persistedReseal);
    assert(!fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Running);
    if (pendingAtBoot) assert(!fuelGaugeRuntime.configurationAllowed);
  }
}

static void checkPorCapacityPersistence() {
  reset();
  persistedReseal = true;
  fuelGaugeSetResealPending(true);
  Wire.setCapacity(1340);
  Wire.words[0x06] |= 0x20;
  fuelGaugeStartDetection(0, true);
  until([] { return fuelGaugeRuntime.probeComplete; });
  fuelGaugeRequestQuiesce();
  until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
  assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
  assert(persistedCapacity == 700 && capacityStoreCalls == 1);
  assert(capacityStoreFlags.front() & 0x20);
  assert(!persistedReseal && Wire.sealed && !(Wire.words[0x06] & 0x30));
  assert(Wire.capacity() == 1340 && !fuelGaugeRuntime.configurationAllowed);
  const FakeWire afterFirstRestartCleanup = Wire;
  reset(true);
  Wire = afterFirstRestartCleanup;
  const int unsealsBeforeSecondBoot = Wire.unsealKeys;
  fuelGaugeStartDetection(persistedCapacity, true);
  until([] { return fuelGaugeRuntime.probeComplete; });
  assert(Wire.unsealKeys == unsealsBeforeSecondBoot && !fuelGaugeRuntime.configurationAllowed);
  fuelGaugeAllowConfiguration();
  ready();
  assert(Wire.capacity() == 700 && fuelGaugeRuntime.desiredCapacity == 700);
  for (bool writeFails : {false, true}) {
    for (bool initiallySealed : {false, true}) {
      reset();
      persistedReseal = true;
      fuelGaugeSetResealPending(true);
      Wire.sealed = initiallySealed;
      Wire.setCapacity(1340);
      Wire.words[0x06] |= 0x20;
      capacityStoreSucceeds = !writeFails;
      capacityReadbackSucceeds = writeFails;
      fuelGaugeStartDetection(0, true);
      until([] { return fuelGaugeRuntime.probeComplete; });
      const unsigned long startedAt = millis();
      fuelGaugeRequestQuiesce();
      until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
      assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed);
      assert(millis() - startedAt <= 8000 && capacityStoreCalls == 1);
      assert(persistedReseal && (Wire.words[0x06] & 0x20) && Wire.sealed);
      assert(!fuelGaugeRuntime.ready && pinLevels[CHRG_CTRL] == HIGH);
      for (uint16_t command : {0x8000, 0x0013, 0x0031, 0x0042}) {
        assert(std::find(Wire.commands.begin(), Wire.commands.end(), command) == Wire.commands.end());
      }
      assert(initiallySealed ? Wire.seals == 0 : Wire.seals > 0);
      for (int index = 0; index < 1000; ++index) { service(); advance(); }
      assert(capacityStoreCalls == 1);
    }
  }
  for (bool queueBeforeFallback : {false, true}) {
    reset();
    persistedReseal = true;
    fuelGaugeSetResealPending(true);
    Wire.setCapacity(1340);
    Wire.words[0x06] |= 0x20;
    fuelGaugeStartDetection(0, true);
    until([] { return fuelGaugeRuntime.probeComplete; });
    if (!queueBeforeFallback) until([] { return capacityStoreCalls != 0; });
    persistedCapacity = 900;
    assert(!fuelGaugeSetCapacity(900));
    fuelGaugeRequestQuiesce();
    until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Complete);
    assert(persistedCapacity == 900 && fuelGaugeRuntime.desiredCapacity == 900);
    assert(capacityStoreCalls == (queueBeforeFallback ? 0 : 1));
    fuelGaugeResume();
    fuelGaugeAllowConfiguration();
    ready();
    assert(Wire.capacity() == 900 && persistedCapacity == 900);
  }
}

static void checkUntrustedResealState() {
  for (bool present : {false, true}) {
    reset();
    fuelGaugeRuntime.sealRequired = true;
    Wire.present = present;
    Wire.words[0x06] |= 0x20;
    failStoreValue = true;
    fuelGaugeStartDetection(700, false);
    until([] { return fuelGaugeRuntime.probeComplete; });
    assert(!fuelGaugeLegacyHardware());
    if (!present) assert(fuelGaugeRuntime.hardware == BatteryHardware::Unknown);
    fuelGaugeRequestQuiesce();
    until([] { return fuelGaugeQuiesceStatus() != FuelGaugeQuiesceStatus::Pending; }, 8100);
    assert(fuelGaugeQuiesceStatus() == FuelGaugeQuiesceStatus::Failed);
    assert(!persistedReseal && !fuelGaugeRuntime.resealPending && fuelGaugeRuntime.sealRequired);
    assert(std::find(Wire.commands.begin(), Wire.commands.end(), 0x8000) == Wire.commands.end());
    assert(Wire.sealed && (Wire.words[0x06] & 0x20));
    if (present) assert(pinLevels[CHRG_CTRL] == HIGH);
  }
}

int main() {
  activeCheck = "probe";
  checkProbeScheduling();
  activeCheck = "acceptance";
  checkDiscoveryAndAcceptance();
  activeCheck = "configuration";
  checkConfiguration();
  activeCheck = "samples";
  checkSamples();
  activeCheck = "queued capacity";
  checkQueuedCapacity();
  activeCheck = "persistence";
  checkPersistence();
  activeCheck = "operation timing";
  checkOperationTiming();
  activeCheck = "identity and invalid configuration";
  checkIdentityAndInvalidConfiguration();
  activeCheck = "transaction failures";
  checkTransactionFailures();
  activeCheck = "cancellation";
  checkCancellationAndRestart();
  activeCheck = "cleanup and menu";
  checkBoundedCleanupAndMenu();
  activeCheck = "stuck cleanup";
  checkStuckCleanup();
  activeCheck = "resumed cleanup";
  checkResumePreservesCleanupDeadline();
  activeCheck = "POR capacity persistence";
  checkPorCapacityPersistence();
  activeCheck = "untrusted reseal state";
  checkUntrustedResealState();
}
