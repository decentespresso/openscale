import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_auto_off_weight_activity import extract_function


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include <initializer_list>

#define CONFIG_APP_ROLLBACK_ENABLE 1
constexpr int ESP_OK = 0;
enum class AppState { PendingVerify, Valid, Invalid };
enum class BatteryRole { Unknown, Legacy, Gauge };
struct Slept {};
struct Halted {};
struct RolledBack {};
struct Restarted {};

AppState appState = AppState::PendingVerify;
bool persistedReseal = true, gaugeAvailable = false, gaugeBoard = true;
bool cleanupAllowed = true, recoveryWritable = true, acceptanceSucceeds = true;
bool recoveryActive = false, usbSleepReturns = false;
bool unsafePinAssigned = false, servesFilesystem = false;
int cleanups = 0, holds = 0, recoveryWrites = 0, acceptances = 0;
int invalidations = 0, restarts = 0, sleeps = 0, attemptClears = 0;
int wifiStops = 0, flushes = 0, oldImageBoots = 0, newImageBoots = 0;
int events = 0, cleanupEvent = 0, holdEvent = 0;

void bootSelectedImage() {
  const bool current = appState == AppState::Valid;
  current ? ++newImageBoots : ++oldImageBoots;
  const BatteryRole role = gaugeAvailable ? BatteryRole::Gauge :
      current && persistedReseal ? BatteryRole::Unknown : BatteryRole::Legacy;
  unsafePinAssigned = gaugeBoard && role == BatteryRole::Legacy;
  servesFilesystem = !current || !recoveryActive;
}

bool quiesceFuelGaugeForShutdown() {
  ++cleanups;
  cleanupEvent = ++events;
  if (!cleanupAllowed || (persistedReseal && !gaugeAvailable)) return false;
  persistedReseal = false;
  return true;
}
void fuelGaugeSleep() {
  assert(cleanups == 1);
  ++holds;
  holdEvent = ++events;
}
bool filesystemRecoveryStore(bool active) {
  assert(active && holds == 1);
  ++recoveryWrites;
  if (recoveryWritable) recoveryActive = active;
  return recoveryWritable;
}
int esp_ota_mark_app_valid_cancel_rollback() {
  assert(recoveryActive);
  ++acceptances;
  if (!acceptanceSucceeds) return -1;
  appState = AppState::Valid;
  return ESP_OK;
}
void hdsOtaClearAttempts() { ++attemptClears; }
void stopWifi() { ++wifiStops; }
struct {
  void print(const char *) {}
  void println(const char *) {}
  void flush() { ++flushes; }
} Serial;
void esp32_sleep() {
  assert(appState == AppState::Valid && recoveryActive && holds == 1);
  assert(wifiStops == 1 && flushes == 1);
  ++sleeps;
  if (!usbSleepReturns) throw Slept{};
}
void esp_ota_mark_app_invalid_rollback_and_reboot() {
  ++invalidations;
  assert(cleanupEvent < holdEvent && holds == 1);
  assert(attemptClears == 1 && wifiStops == 1 && flushes == 1);
  appState = AppState::Invalid;
  bootSelectedImage();
  throw RolledBack{};
}
void delay(unsigned long) { throw Halted{}; }
struct {
  void restart() { ++restarts; throw Restarted{}; }
} ESP;

ROLLBACK_FUNCTION

void reset(AppState state) {
  appState = state;
  persistedReseal = true;
  gaugeAvailable = false;
  gaugeBoard = true;
  cleanupAllowed = true;
  recoveryWritable = acceptanceSucceeds = true;
  recoveryActive = usbSleepReturns = false;
  unsafePinAssigned = servesFilesystem = false;
  cleanups = holds = recoveryWrites = acceptances = 0;
  invalidations = restarts = sleeps = attemptClears = 0;
  wifiStops = flushes = oldImageBoots = newImageBoots = 0;
  events = cleanupEvent = holdEvent = 0;
}

void expectBlocked(bool expectSleep) {
  bool stopped = false;
  try {
    hdsOtaRollback("failed cleanup");
  } catch (const Slept &) {
    assert(expectSleep);
    stopped = true;
  } catch (const Halted &) {
    assert(!expectSleep);
    stopped = true;
  }
  assert(stopped && persistedReseal);
  assert(cleanups == 1 && holds == 1 && cleanupEvent < holdEvent);
  assert(invalidations == 0 && restarts == 0 && attemptClears == 0);
  assert(wifiStops == 1 && flushes == 1 && oldImageBoots == 0);
}

int main() {
  for (AppState initial : {AppState::PendingVerify, AppState::Valid}) {
    reset(initial);
    bootSelectedImage();
    if (initial == AppState::PendingVerify) {
      assert(oldImageBoots == 1 && unsafePinAssigned);
    }
    reset(initial);
    expectBlocked(true);
    assert(recoveryWrites == 1 && acceptances == 1 && sleeps == 1);
    bootSelectedImage();
    assert(newImageBoots == 1 && oldImageBoots == 0);
    assert(!unsafePinAssigned && !servesFilesystem);

    reset(initial);
    usbSleepReturns = true;
    expectBlocked(false);
    assert(sleeps == 1 && acceptances == 1 && recoveryActive);
    bootSelectedImage();
    assert(newImageBoots == 1 && !unsafePinAssigned && !servesFilesystem);

    for (bool recoveryFails : {false, true}) {
      reset(initial);
      recoveryWritable = !recoveryFails;
      acceptanceSucceeds = recoveryFails;
      expectBlocked(false);
      assert(sleeps == 0 && recoveryWrites == 1);
      assert(acceptances == (recoveryFails ? 0 : 1));
      assert(appState == initial && recoveryActive == !recoveryFails);
    }

    reset(initial);
    gaugeAvailable = true;
    cleanupAllowed = false;
    expectBlocked(true);
    assert(persistedReseal && recoveryActive && appState == AppState::Valid);

    for (bool legacy : {false, true}) {
      reset(initial);
      gaugeBoard = !legacy;
      gaugeAvailable = !legacy;
      persistedReseal = !legacy;
      bool rolledBack = false;
      try { hdsOtaRollback("clean rollback"); }
      catch (const RolledBack &) { rolledBack = true; }
      assert(rolledBack && invalidations == 1 && oldImageBoots == 1);
      assert(!persistedReseal && !unsafePinAssigned);
      assert(recoveryWrites == 0 && acceptances == 0 && sleeps == 0);
      assert(restarts == 0 && newImageBoots == 0);
    }
  }
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    rollback = (ROOT / "include/ota_rollback.h").read_text(encoding="utf-8")
    function = extract_function(rollback, "static void hdsOtaRollback(")
    with tempfile.TemporaryDirectory(prefix="rollback-reseal-safety-") as directory:
        cpp = Path(directory) / "rollback.cpp"
        binary = Path(directory) / ("rollback.exe" if os.name == "nt" else "rollback")
        cpp.write_text(HARNESS.replace("ROLLBACK_FUNCTION", function), encoding="utf-8")
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("Rollback reseal safety, old-image exclusion, and persistence failure tests passed")


if __name__ == "__main__":
    main()
