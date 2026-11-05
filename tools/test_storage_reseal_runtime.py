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
#include <set>
#include <string>

#define HDS_ENABLE_ENERGY_MENU 0
using String = std::string;
constexpr float CALIBRATION_VALUE_DEFAULT = 917.94f;
constexpr const char *WELCOME1 = "Half Decent";
using nvs_handle_t = uint32_t;
using esp_err_t = int;
constexpr int NVS_READONLY = 0;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 2, ESP_ERR_NVS_TYPE_MISMATCH = 3;

CONSTANTS

int writes = 0, migrations = 0, opens = 0, reads = 0, closes = 0;
bool writable = false, namespaceAvailable = true;
esp_err_t openResult = ESP_OK, readResult = ESP_ERR_NVS_NOT_FOUND;
uint8_t markerValue = 0;
struct {
  std::set<std::string> keys;
  uint16_t schema = STORAGE_SCHEMA_VERSION;
  String storedString;
  bool begin(const char *name, bool readOnly) {
    assert(std::strcmp(name, STORAGE_NAMESPACE) == 0 && !readOnly);
    return namespaceAvailable;
  }
  bool isKey(const char *key) { return keys.count(key) != 0; }
  size_t put(const char *key, size_t bytes) {
    ++writes;
    if (!writable) return 0;
    keys.insert(key);
    return bytes;
  }
  size_t putFloat(const char *key, float) { return put(key, sizeof(float)); }
  size_t putInt(const char *key, int) { return put(key, sizeof(int)); }
  size_t putBool(const char *key, bool) { return put(key, sizeof(bool)); }
  size_t putString(const char *key, const String &value) {
    if (writable) storedString = value;
    return put(key, value.size());
  }
  String getString(const char *key, const String &fallback) {
    return isKey(key) ? storedString : fallback;
  }
  uint16_t getUShort(const char *key, uint16_t fallback) {
    return isKey(key) ? schema : fallback;
  }
  size_t putUShort(const char *key, uint16_t value) {
    if (writable) schema = value;
    return put(key, sizeof(uint16_t));
  }
} settingsPreferences;

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
  ++opens;
  assert(std::strcmp(name, STORAGE_NAMESPACE) == 0 && mode == NVS_READONLY);
  if (openResult == ESP_OK) *handle = 7;
  return openResult;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) {
  ++reads;
  assert(handle == 7 && std::strcmp(key, KEY_BAT_RESEAL) == 0);
  if (readResult == ESP_OK) *value = markerValue;
  return readResult;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 7); ++closes; }
bool storageMigrateLegacyEeprom() { ++migrations; return false; }

FUNCTIONS

static void reset() {
  settingsPreferences.keys = {
    KEY_STORAGE_SCHEMA, KEY_CAL1, KEY_CONTAINER, KEY_MODE, KEY_POUROVER,
    KEY_ESPRESSO, KEY_BEEP, KEY_WELCOME, KEY_BAT_CAL, KEY_HEARTBEAT,
    KEY_SCREEN_FLIP, KEY_TIME_ON_TOP, KEY_APP_TEXT, KEY_BTN_CONN, KEY_WIFI_BOOT,
    KEY_AUTO_SLEEP, KEY_QUICK_BOOT, KEY_DRIFT_MAX, KEY_TAP_TARE, KEY_TAP_TIMER,
    KEY_WOW_INTERVAL, KEY_BAT_PROTECT, KEY_BAT_CAPACITY_SET, KEY_BAT_CAPACITY
  };
  settingsPreferences.schema = STORAGE_SCHEMA_VERSION;
  writes = migrations = opens = reads = closes = 0;
  writable = false;
  namespaceAvailable = true;
  openResult = ESP_OK;
  readResult = ESP_ERR_NVS_NOT_FOUND;
  markerValue = 0;
}

int main() {
  reset();
  bool opened = false;
  assert(storageInit(&opened) && opened);
  assert(storageHasAllSettings() && !settingsPreferences.isKey(KEY_BAT_RESEAL));
  assert(writes == 0 && migrations == 0);
  assert(storageGetResealState() == 0);
  assert(opens == 1 && reads == 1 && closes == 1 && writes == 0);

  for (uint8_t value : {0, 1, 2, 255}) {
    reset();
    readResult = ESP_OK;
    markerValue = value;
    assert(storageGetResealState() == (value <= 1 ? value : 2));
    assert(opens == 1 && reads == 1 && closes == 1 && writes == 0);
  }
  for (esp_err_t error : {ESP_ERR_NVS_TYPE_MISMATCH, ESP_FAIL}) {
    reset();
    readResult = error;
    assert(storageGetResealState() == 2);
    assert(opens == 1 && reads == 1 && closes == 1 && writes == 0);
  }
  for (esp_err_t error : {ESP_ERR_NVS_NOT_FOUND, ESP_FAIL}) {
    reset();
    openResult = error;
    assert(storageGetResealState() == 2);
    assert(opens == 1 && reads == 0 && closes == 0 && writes == 0);
  }

  reset();
  settingsPreferences.keys.erase(KEY_CAL1);
  assert(!storageInit(&opened) && opened);
  assert(writes == 1 && migrations == 0);
  reset();
  namespaceAvailable = false;
  opened = true;
  assert(!storageInit(&opened) && !opened && writes == 0);
  reset();
  settingsPreferences.keys.erase(KEY_STORAGE_SCHEMA);
  writable = true;
  assert(storageInit(&opened) && opened);
  assert(writes == 1 && migrations == 0 && !settingsPreferences.isKey(KEY_BAT_RESEAL));
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    storage = (ROOT / "include/storage.h").read_text(encoding="utf-8")
    constants = storage[storage.index("constexpr const char *STORAGE_NAMESPACE"):
                        storage.index("constexpr size_t LEGACY_EEPROM_BYTES")]
    signatures = (
        "inline float storageBatteryCalibrationDefault()",
        "inline bool storagePutFloat(", "inline bool storagePutInt(",
        "inline bool storagePutBool(", "inline bool storagePutString(",
        "inline uint8_t storageGetResealState()",
        "inline bool storageEnsureFloat(", "inline bool storageEnsureInt(",
        "inline bool storageEnsureBool(", "inline bool storageEnsureString(",
        "inline bool storageHasAllSettings()", "inline bool storageEnsureDefaults()",
        "inline bool storageInit(",
    )
    functions = "\n".join(extract_function(storage, signature) for signature in signatures)
    assert "KEY_BAT_RESEAL" not in extract_function(storage, "inline bool storageMigrateLegacyEeprom()")
    source = HARNESS.replace("CONSTANTS", constants).replace("FUNCTIONS", functions)
    with tempfile.TemporaryDirectory(prefix="storage-reseal-") as directory:
        cpp = Path(directory) / "storage.cpp"
        binary = Path(directory) / ("storage.exe" if os.name == "nt" else "storage")
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Optional reseal storage, full NVS, and error-aware read checks passed")


if __name__ == "__main__":
    main()
