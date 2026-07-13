#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <EEPROM.h>
#include <Preferences.h>
#include <math.h>
#include "calibration_validation.h"
#include "config.h"

constexpr const char *STORAGE_NAMESPACE = "hds";
constexpr const char *KEY_STORAGE_SCHEMA = "schema";
constexpr uint16_t STORAGE_SCHEMA_VERSION = 1;
constexpr const char *KEY_CAL1 = "cal1";
constexpr const char *KEY_CONTAINER = "container";
constexpr const char *KEY_MODE = "mode";
constexpr const char *KEY_POUROVER = "pourover";
constexpr const char *KEY_ESPRESSO = "espresso";
constexpr const char *KEY_BEEP = "beep";
constexpr const char *KEY_WELCOME = "welcome";
constexpr const char *KEY_BAT_CAL = "bat_cal";
constexpr const char *KEY_HEARTBEAT = "heartbeat";
constexpr const char *KEY_SCREEN_FLIP = "screen_flip";
constexpr const char *KEY_TIME_ON_TOP = "time_on_top";
constexpr const char *KEY_BTN_CONN = "btn_conn";
constexpr const char *KEY_WIFI_BOOT = "wifi_boot";
constexpr const char *KEY_AUTO_SLEEP = "auto_sleep";
constexpr const char *KEY_QUICK_BOOT = "quick_boot";
constexpr const char *KEY_DRIFT_MAX = "drift_max";

constexpr size_t LEGACY_EEPROM_BYTES = 512;
constexpr size_t LEGACY_CAL1_ADDRESS = 0;
constexpr size_t LEGACY_CONTAINER_ADDRESS = LEGACY_CAL1_ADDRESS + sizeof(float);
constexpr size_t LEGACY_MODE_ADDRESS = LEGACY_CONTAINER_ADDRESS + sizeof(float);
constexpr size_t LEGACY_POUROVER_ADDRESS = LEGACY_MODE_ADDRESS + sizeof(int);
constexpr size_t LEGACY_ESPRESSO_ADDRESS = LEGACY_POUROVER_ADDRESS + sizeof(float);
constexpr size_t LEGACY_BEEP_ADDRESS = LEGACY_ESPRESSO_ADDRESS + sizeof(float);
constexpr size_t LEGACY_WELCOME_ADDRESS = LEGACY_BEEP_ADDRESS + sizeof(int);
constexpr size_t LEGACY_WELCOME_BYTES = 128;
constexpr size_t LEGACY_BAT_CAL_ADDRESS = LEGACY_WELCOME_ADDRESS + LEGACY_WELCOME_BYTES;
constexpr size_t LEGACY_HEARTBEAT_ADDRESS = LEGACY_BAT_CAL_ADDRESS + sizeof(float);
constexpr size_t LEGACY_SCREEN_FLIP_ADDRESS = LEGACY_HEARTBEAT_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_TIME_ON_TOP_ADDRESS = LEGACY_SCREEN_FLIP_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_BTN_CONN_ADDRESS = LEGACY_TIME_ON_TOP_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_WIFI_BOOT_ADDRESS = LEGACY_BTN_CONN_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_AUTO_SLEEP_ADDRESS = LEGACY_WIFI_BOOT_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_QUICK_BOOT_ADDRESS = LEGACY_AUTO_SLEEP_ADDRESS + sizeof(bool);
constexpr size_t LEGACY_DRIFT_MAX_ADDRESS = LEGACY_QUICK_BOOT_ADDRESS + sizeof(bool);

static_assert(sizeof(float) == 4);
static_assert(sizeof(int) == 4);
static_assert(sizeof(bool) == 1);

extern Preferences settingsPreferences;

inline float storageBatteryCalibrationDefault() {
#ifdef V7_2
  return 1.66f;
#else
  return 1.06f;
#endif
}

inline bool storagePutFloat(const char *key, float value) {
  return settingsPreferences.putFloat(key, value) == sizeof(float);
}

inline bool storagePutInt(const char *key, int value) {
  return settingsPreferences.putInt(key, value) == sizeof(int);
}

inline bool storagePutBool(const char *key, bool value) {
  return settingsPreferences.putBool(key, value) == sizeof(bool);
}

inline bool storagePutString(const char *key, const String &value) {
  settingsPreferences.putString(key, value);
  return settingsPreferences.isKey(key) &&
         settingsPreferences.getString(key, "\x01") == value;
}

inline float storageGetFloat(const char *key, float defaultValue) {
  return settingsPreferences.getFloat(key, defaultValue);
}

inline int storageGetInt(const char *key, int defaultValue) {
  return settingsPreferences.getInt(key, defaultValue);
}

inline bool storageGetBool(const char *key, bool defaultValue) {
  return settingsPreferences.getBool(key, defaultValue);
}

inline String storageGetString(const char *key, const String &defaultValue) {
  return settingsPreferences.getString(key, defaultValue);
}

inline bool storageEnsureFloat(const char *key, float value) {
  return settingsPreferences.isKey(key) || storagePutFloat(key, value);
}

inline bool storageEnsureInt(const char *key, int value) {
  return settingsPreferences.isKey(key) || storagePutInt(key, value);
}

inline bool storageEnsureBool(const char *key, bool value) {
  return settingsPreferences.isKey(key) || storagePutBool(key, value);
}

inline bool storageEnsureString(const char *key, const String &value) {
  return settingsPreferences.isKey(key) || storagePutString(key, value);
}

inline bool storageHasAllSettings() {
  const char *keys[] = {
    KEY_CAL1, KEY_CONTAINER, KEY_MODE, KEY_POUROVER, KEY_ESPRESSO,
    KEY_BEEP, KEY_WELCOME, KEY_BAT_CAL, KEY_HEARTBEAT, KEY_SCREEN_FLIP,
    KEY_TIME_ON_TOP, KEY_BTN_CONN, KEY_WIFI_BOOT, KEY_AUTO_SLEEP,
    KEY_QUICK_BOOT, KEY_DRIFT_MAX
  };
  for (const char *key : keys) {
    if (!settingsPreferences.isKey(key)) {
      return false;
    }
  }
  return true;
}

inline bool storageEnsureDefaults() {
  return storageEnsureFloat(KEY_CAL1, CALIBRATION_VALUE_DEFAULT) &&
         storageEnsureFloat(KEY_CONTAINER, 0.0f) &&
         storageEnsureInt(KEY_MODE, 0) &&
         storageEnsureFloat(KEY_POUROVER, 16.0f) &&
         storageEnsureFloat(KEY_ESPRESSO, 18.0f) &&
         storageEnsureInt(KEY_BEEP, 1) &&
         storageEnsureString(KEY_WELCOME, String(WELCOME1)) &&
         storageEnsureFloat(KEY_BAT_CAL, storageBatteryCalibrationDefault()) &&
         storageEnsureBool(KEY_HEARTBEAT, true) &&
         storageEnsureBool(KEY_SCREEN_FLIP, false) &&
         storageEnsureBool(KEY_TIME_ON_TOP, false) &&
         storageEnsureBool(KEY_BTN_CONN, false) &&
         storageEnsureBool(KEY_WIFI_BOOT, false) &&
         storageEnsureBool(KEY_AUTO_SLEEP, true) &&
         storageEnsureBool(KEY_QUICK_BOOT, false) &&
         storageEnsureFloat(KEY_DRIFT_MAX, 0.05f);
}

inline bool storageLegacyBool(size_t address, bool defaultValue) {
  uint8_t value = 0xff;
  EEPROM.get(address, value);
  return value <= 1 ? value == 1 : defaultValue;
}

inline String storageLegacyWelcome() {
  constexpr char magic[] = {'H', 'D', 'S', 'W'};
  constexpr size_t magicBytes = sizeof(magic);
  constexpr size_t textBytes = LEGACY_WELCOME_BYTES - magicBytes;
  for (size_t index = 0; index < magicBytes; ++index) {
    if (EEPROM.read(LEGACY_WELCOME_ADDRESS + index) != (uint8_t)magic[index]) {
      return String(WELCOME1);
    }
  }
  char text[textBytes];
  size_t length = 0;
  for (; length < textBytes - 1; ++length) {
    uint8_t value = EEPROM.read(LEGACY_WELCOME_ADDRESS + magicBytes + length);
    if (value == 0) {
      break;
    }
    if (value < 0x20 || value > 0x7e) {
      return String(WELCOME1);
    }
    text[length] = (char)value;
  }
  text[length] = 0;
  String welcome(text);
  welcome.trim();
  return welcome.length() > 0 ? welcome : String(WELCOME1);
}

inline bool storageMigrateLegacyEeprom() {
  if (!EEPROM.begin(LEGACY_EEPROM_BYTES)) {
    return false;
  }

  float calibration = CALIBRATION_VALUE_DEFAULT;
  float container = 0.0f;
  int mode = 0;
  float pourover = 16.0f;
  float espresso = 18.0f;
  int beep = 1;
  float batteryCalibration = storageBatteryCalibrationDefault();
  float driftMax = 0.05f;
  EEPROM.get(LEGACY_CAL1_ADDRESS, calibration);
  EEPROM.get(LEGACY_CONTAINER_ADDRESS, container);
  EEPROM.get(LEGACY_MODE_ADDRESS, mode);
  EEPROM.get(LEGACY_POUROVER_ADDRESS, pourover);
  EEPROM.get(LEGACY_ESPRESSO_ADDRESS, espresso);
  EEPROM.get(LEGACY_BEEP_ADDRESS, beep);
  EEPROM.get(LEGACY_BAT_CAL_ADDRESS, batteryCalibration);
  EEPROM.get(LEGACY_DRIFT_MAX_ADDRESS, driftMax);

  if (!isValidCalibrationValue(calibration)) calibration = CALIBRATION_VALUE_DEFAULT;
  if (!isfinite(container)) container = 0.0f;
  if (mode < 0 || mode > 1) mode = 0;
  if (!isfinite(pourover)) pourover = 16.0f;
  if (!isfinite(espresso)) espresso = 18.0f;
  if (beep < 0 || beep > 1) beep = 1;
#ifdef V7_2
  if (!isfinite(batteryCalibration) || batteryCalibration < 1.4f || batteryCalibration > 1.8f) {
    batteryCalibration = 1.66f;
  }
#else
  if (!isfinite(batteryCalibration) || batteryCalibration < 0.9f || batteryCalibration > 1.3f) {
    batteryCalibration = 1.06f;
  }
#endif
  if (!isfinite(driftMax)) driftMax = 0.05f;

  bool migrated = storageEnsureFloat(KEY_CAL1, calibration) &&
                  storageEnsureFloat(KEY_CONTAINER, container) &&
                  storageEnsureInt(KEY_MODE, mode) &&
                  storageEnsureFloat(KEY_POUROVER, pourover) &&
                  storageEnsureFloat(KEY_ESPRESSO, espresso) &&
                  storageEnsureInt(KEY_BEEP, beep) &&
                  storageEnsureString(KEY_WELCOME, storageLegacyWelcome()) &&
                  storageEnsureFloat(KEY_BAT_CAL, batteryCalibration) &&
                  storageEnsureBool(KEY_HEARTBEAT, storageLegacyBool(LEGACY_HEARTBEAT_ADDRESS, true)) &&
                  storageEnsureBool(KEY_SCREEN_FLIP, storageLegacyBool(LEGACY_SCREEN_FLIP_ADDRESS, false)) &&
                  storageEnsureBool(KEY_TIME_ON_TOP, storageLegacyBool(LEGACY_TIME_ON_TOP_ADDRESS, false)) &&
                  storageEnsureBool(KEY_BTN_CONN, storageLegacyBool(LEGACY_BTN_CONN_ADDRESS, false)) &&
                  storageEnsureBool(KEY_WIFI_BOOT, storageLegacyBool(LEGACY_WIFI_BOOT_ADDRESS, false)) &&
                  storageEnsureBool(KEY_AUTO_SLEEP, storageLegacyBool(LEGACY_AUTO_SLEEP_ADDRESS, true)) &&
                  storageEnsureBool(KEY_QUICK_BOOT, storageLegacyBool(LEGACY_QUICK_BOOT_ADDRESS, false)) &&
                  storageEnsureFloat(KEY_DRIFT_MAX, driftMax);
  EEPROM.end();
  return migrated;
}

inline bool storageInit() {
  if (!settingsPreferences.begin(STORAGE_NAMESPACE, false)) {
    return false;
  }
  if (settingsPreferences.getUShort(KEY_STORAGE_SCHEMA, 0) >= STORAGE_SCHEMA_VERSION) {
    return storageEnsureDefaults();
  }
  if (!storageHasAllSettings() && !storageMigrateLegacyEeprom()) {
    return false;
  }
  if (!storageHasAllSettings()) {
    return false;
  }
  return settingsPreferences.putUShort(KEY_STORAGE_SCHEMA, STORAGE_SCHEMA_VERSION) == sizeof(uint16_t);
}

inline bool storagePutWelcome(const String &value) {
  String welcome = value;
  welcome.trim();
  if (welcome.length() > LEGACY_WELCOME_BYTES - 5) {
    welcome.remove(LEGACY_WELCOME_BYTES - 5);
  }
  if (welcome.length() == 0) {
    welcome = String(WELCOME1);
  }
  return storagePutString(KEY_WELCOME, welcome);
}

#endif
