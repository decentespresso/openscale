import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_auto_off_weight_activity import extract_function


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#define CONFIG_APP_ROLLBACK_ENABLE 1
#define HDS_ENABLE_ENERGY_MENU 1
constexpr const char *STORAGE_NAMESPACE = "hds";
constexpr const char *KEY_STORAGE_SCHEMA = "schema";
constexpr const char *KEY_BAT_RESEAL = "bat_reseal";
constexpr uint16_t STORAGE_SCHEMA_VERSION = 1;
constexpr int ESP_OK = 0;
struct Halted {};
struct Slept {};
struct RolledBack {};
bool namespaceAvailable = true, defaultsWritable = true, energyLoadSucceeds = true;
bool selectedRollback = true, preDetectionAbort = false, cleanupSucceeds = true;
bool storedMarkerPresent = false, storedMarkerTyped = true;
bool recoveryStored = false, rollbackCancelled = false;
uint8_t storedMarkerValue = 0;
int energyLoads = 0, energyInitializations = 0, discoveries = 0, cleanupCalls = 0;
int shutdownHolds = 0, wifiStops = 0, events = 0, cleanupEvent = 0, holdEvent = 0;
struct {
  bool opened = false;
  bool begin(const char *, bool) { opened = namespaceAvailable; return opened; }
  uint16_t getUShort(const char *, uint16_t) { return STORAGE_SCHEMA_VERSION; }
  size_t putUShort(const char *, uint16_t) { return 2; }
  bool isKey(const char *) { return opened && storedMarkerPresent; }
  uint8_t getUChar(const char *, uint8_t fallback) {
    return opened && storedMarkerPresent && storedMarkerTyped ? storedMarkerValue : fallback;
  }
} settingsPreferences;
bool storageEnsureDefaults() {
  return defaultsWritable;
}
uint8_t storageGetResealState() {
  if (!settingsPreferences.opened) return 2;
  if (!storedMarkerPresent) return 0;
  return storedMarkerTyped && storedMarkerValue <= 1 ? storedMarkerValue : 2;
}
bool storageHasAllSettings() { return true; }
bool storageMigrateLegacyEeprom() { return true; }
struct {
  void print(const char *) {}
  void println(const char *) {}
  void flush() {}
} Serial;
void delay(unsigned long) { throw Halted{}; }
unsigned long millis() { return 0; }
enum class EnergyFeature { UsbSleepTest };
struct EnergySettings {};
struct {
  EnergySettings settings;
  bool featureEnabled(EnergyFeature) { return false; }
  void begin(unsigned long) { ++energyInitializations; }
} energyPolicy;
bool energyLoadSettings(EnergySettings &) { ++energyLoads; return energyLoadSucceeds; }
bool initEnergyPowerManagement() { ++energyInitializations; return true; }
bool applyEnergyLightSleepSetting(bool) { ++energyInitializations; return true; }
void refreshEnergyMenuRows() { ++energyInitializations; }
struct { bool sealRequired = false, resealPending = false; } fuelGaugeRuntime;
void fuelGaugeSetResealPending(bool pending) {
  fuelGaugeRuntime.resealPending = pending;
  fuelGaugeRuntime.sealRequired = pending;
}
bool quiesceFuelGaugeForShutdown() {
  ++cleanupCalls;
  cleanupEvent = ++events;
  return cleanupSucceeds;
}
void fuelGaugeSleep() { ++shutdownHolds; holdEvent = ++events; }
bool filesystemRecoveryStore(bool active) { recoveryStored = active; return true; }
int esp_ota_mark_app_valid_cancel_rollback() { rollbackCancelled = true; return ESP_OK; }
void esp32_sleep() { throw Slept{}; }
void hdsOtaClearAttempts() {}
void stopWifi() { ++wifiStops; }
void esp_ota_mark_app_invalid_rollback_and_reboot() {
  assert(discoveries == 1 && cleanupCalls == 1 && shutdownHolds == 1);
  assert(cleanupEvent < holdEvent && wifiStops == 1);
  throw RolledBack{};
}
struct { void restart() {} } ESP;

__STORAGE_INIT__

__ROLLBACK_FUNCTION__

static void boot() {
  const char *bootRollbackReason = selectedRollback ? "boot attempts" : nullptr;
__SETTINGS_BLOCK__
  if (preDetectionAbort) throw Slept{};
__RESEAL_BLOCK__
  ++discoveries;
__DEFERRED_ROLLBACK__
}

static void reset() {
  namespaceAvailable = true;
  defaultsWritable = true;
  energyLoadSucceeds = true;
  selectedRollback = true;
  preDetectionAbort = false;
  cleanupSucceeds = true;
  storedMarkerPresent = false;
  storedMarkerTyped = true;
  storedMarkerValue = 0;
  recoveryStored = rollbackCancelled = false;
  energyLoads = energyInitializations = discoveries = cleanupCalls = 0;
  shutdownHolds = wifiStops = events = cleanupEvent = holdEvent = 0;
  settingsPreferences.opened = false;
  fuelGaugeRuntime = {};
}

static void expectRollback() {
  bool rolledBack = false;
  try { boot(); } catch (const RolledBack &) { rolledBack = true; }
  assert(rolledBack && energyLoads == 0 && energyInitializations == 0);
}

static void expectBlockedRollback() {
  bool slept = false;
  try { boot(); } catch (const Slept &) { slept = true; }
  assert(slept && recoveryStored && rollbackCancelled);
  assert(discoveries == 1 && cleanupCalls == 1 && shutdownHolds == 1);
  assert(cleanupEvent < holdEvent && wifiStops == 1);
  assert(energyLoads == 0 && energyInitializations == 0);
}

int main() {
  reset();
  defaultsWritable = false;
  expectRollback();
  assert(!storedMarkerPresent && !fuelGaugeRuntime.sealRequired);
  reset();
  namespaceAvailable = false;
  cleanupSucceeds = false;
  expectBlockedRollback();
  assert(fuelGaugeRuntime.sealRequired && !fuelGaugeRuntime.resealPending);
  reset();
  storedMarkerPresent = true;
  storedMarkerValue = 1;
  expectRollback();
  assert(fuelGaugeRuntime.sealRequired && fuelGaugeRuntime.resealPending);
  for (bool typed : {false, true}) {
    reset();
    storedMarkerPresent = true;
    storedMarkerTyped = typed;
    storedMarkerValue = 2;
    expectRollback();
    assert(fuelGaugeRuntime.sealRequired && !fuelGaugeRuntime.resealPending);
  }
  reset();
  energyLoadSucceeds = false;
  expectRollback();
  for (bool storageFailure : {false, true}) {
    reset();
    selectedRollback = false;
    defaultsWritable = !storageFailure;
    energyLoadSucceeds = storageFailure;
    bool halted = false;
    try { boot(); } catch (const Halted &) { halted = true; }
    assert(halted && discoveries == 0 && cleanupCalls == 0 && shutdownHolds == 0);
    assert(energyLoads == (storageFailure ? 0 : 1));
  }
  reset();
  selectedRollback = false;
  boot();
  assert(energyLoads == 1 && energyInitializations > 0 && discoveries == 1);
  for (bool available : {false, true}) {
    reset();
    namespaceAvailable = available;
    defaultsWritable = false;
    preDetectionAbort = true;
    bool slept = false;
    try { boot(); } catch (const Slept &) { slept = true; }
    assert(slept && discoveries == 0 && cleanupCalls == 0 && shutdownHolds == 0);
  }
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    setup = extract_function((ROOT / "src/hds.ino").read_text(encoding="utf-8"), "void setup()")
    storage = (ROOT / "include/storage.h").read_text(encoding="utf-8")
    rollback = (ROOT / "include/ota_rollback.h").read_text(encoding="utf-8")
    settings = setup[setup.index("  bool bootSettingsOpened = false;"):setup.index("#if HDS_ENABLE_GRINDER")]
    reseal = setup[setup.index("  const uint8_t bootResealState"):setup.index("  const int storedBatteryCapacity")]
    deferred = "if (bootRollbackReason) hdsOtaRollback(bootRollbackReason);"
    assert setup.index("esp32_sleep();") < setup.index("Wire.begin(") < setup.index(reseal)
    assert setup.index("fuelGaugeStartDetection(") < setup.index(deferred)
    assert "KEY_BAT_RESEAL" not in extract_function(storage, "inline bool storageEnsureDefaults()")
    source = HARNESS.replace("__STORAGE_INIT__", extract_function(storage, "inline bool storageInit("))
    source = source.replace("__ROLLBACK_FUNCTION__", extract_function(rollback, "static void hdsOtaRollback("))
    source = source.replace("__SETTINGS_BLOCK__", settings).replace("__RESEAL_BLOCK__", reseal)
    source = source.replace("__DEFERRED_ROLLBACK__", deferred)
    with tempfile.TemporaryDirectory(prefix="boot-rollback-settings-") as directory:
        cpp = Path(directory) / "boot.cpp"
        binary = Path(directory) / ("boot.exe" if os.name == "nt" else "boot")
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Boot rollback settings failure runtime checks passed")


if __name__ == "__main__":
    main()
