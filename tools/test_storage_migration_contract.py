from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STORAGE = (ROOT / "include" / "storage.h").read_text(encoding="utf-8")


def require(text, source=STORAGE):
    assert text in source, f"missing storage contract: {text}"


require('STORAGE_NAMESPACE = "hds"')
assert '"cal2"' not in STORAGE
assert '"ntc_cal"' not in STORAGE
require('KEY_STORAGE_SCHEMA = "schema"')
require("STORAGE_SCHEMA_VERSION = 1")

for key, value in {
    "KEY_CAL1": "cal1",
    "KEY_CONTAINER": "container",
    "KEY_MODE": "mode",
    "KEY_POUROVER": "pourover",
    "KEY_ESPRESSO": "espresso",
    "KEY_BEEP": "beep",
    "KEY_WELCOME": "welcome",
    "KEY_BAT_CAL": "bat_cal",
    "KEY_HEARTBEAT": "heartbeat",
    "KEY_SCREEN_FLIP": "screen_flip",
    "KEY_TIME_ON_TOP": "time_on_top",
    "KEY_BTN_CONN": "btn_conn",
    "KEY_WIFI_BOOT": "wifi_boot",
    "KEY_AUTO_SLEEP": "auto_sleep",
    "KEY_QUICK_BOOT": "quick_boot",
    "KEY_DRIFT_MAX": "drift_max",
}.items():
    require(f'{key} = "{value}"')

for address, value in {
    "LEGACY_CAL1_ADDRESS": "0",
    "LEGACY_WELCOME_ADDRESS": "LEGACY_BEEP_ADDRESS + sizeof(int)",
    "LEGACY_WELCOME_BYTES": "128",
    "LEGACY_DRIFT_MAX_ADDRESS": "LEGACY_QUICK_BOOT_ADDRESS + sizeof(bool)",
}.items():
    require(f"{address} = {value}")

assert STORAGE.index("EEPROM.end();") < STORAGE.rindex(
    "putUShort(KEY_STORAGE_SCHEMA, STORAGE_SCHEMA_VERSION)"
)
require("settingsPreferences.isKey(key) || storagePutFloat(key, value)")
require("settingsPreferences.isKey(key) || storagePutBool(key, value)")

for relative_path in (
    "src/hds.ino",
    "include/parameter.h",
    "include/menu.h",
    "include/usbcomm.h",
    "include/ble.h",
):
    source = (ROOT / relative_path).read_text(encoding="utf-8")
    assert "EEPROM" not in source, f"runtime EEPROM access in {relative_path}"

print("storage migration contract: ok")
