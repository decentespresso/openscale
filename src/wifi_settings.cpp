#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include <Preferences.h>
#include "wifi_setup.h"
#include "wifi_settings.h"

void WiFiParams::init() {
  wifiInitLocks();
  xSemaphoreTake(wifiSettingsMutex, portMAX_DELAY);
  if (!initialized) {
    Preferences preferences;
    if (preferences.begin(wifiPrefsKey, false)) {
      WifiCredentials record;
      if (preferences.getBytesLength("credentials") == sizeof(record) &&
          preferences.getBytes("credentials", &record, sizeof(record)) == sizeof(record) &&
          wifiCredentialRecordValid(record)) {
        ssid = record.ssid;
        pass = record.pass;
      } else if (!preferences.isKey("credentials")) {
        ssid = preferences.getString(wifiSSIDKey, "");
        pass = preferences.getString(wifiPassKey, "");
      } else {
        Serial.println("[prefs] invalid WiFi credential record");
      }
      char storedName[MDNS_NAME_BUFFER_BYTES] = {};
      preferences.getString(wifiMdnsNameKey, storedName, sizeof(storedName));
      if (!mdnsNameNormalize(storedName, mdnsName, sizeof(mdnsName))) {
        mdnsNameCopyDefault(mdnsName, sizeof(mdnsName));
      }
      preferences.end();
      initialized = true;
    }
  }
  xSemaphoreGive(wifiSettingsMutex);
}

bool WiFiParams::saveCredentials(const String &newSsid, const String &newPass) {
  init();
  xSemaphoreTake(wifiSettingsMutex, portMAX_DELAY);
  if (!initialized || newSsid.length() > 32 || newPass.length() > 64) {
    xSemaphoreGive(wifiSettingsMutex);
    return false;
  }
  WifiCredentials record;
  memcpy(record.ssid, newSsid.c_str(), newSsid.length() + 1);
  memcpy(record.pass, newPass.c_str(), newPass.length() + 1);
  Preferences preferences;
  bool saved = preferences.begin(wifiPrefsKey, false);
  if (saved) {
    WifiCredentials previous;
    const bool hadRecord = preferences.isKey("credentials");
    const bool previousValid = !hadRecord ||
        (preferences.getBytes("credentials", &previous, sizeof(previous)) == sizeof(previous) &&
         wifiCredentialRecordValid(previous));
    if (!previousValid) previous = {};
    if (!hadRecord && ssid.length() <= 32 && pass.length() <= 64) {
      memcpy(previous.ssid, ssid.c_str(), ssid.length() + 1);
      memcpy(previous.pass, pass.c_str(), pass.length() + 1);
    }
    WifiCredentials stored;
    saved =
        preferences.putBytes("credentials", &record, sizeof(record)) == sizeof(record) &&
        preferences.getBytes("credentials", &stored, sizeof(stored)) == sizeof(stored) &&
        memcmp(&record, &stored, sizeof(record)) == 0;
    const bool clearingLegacy = saved && newSsid.length() == 0;
    if (clearingLegacy) {
      const bool ssidRemoved = !preferences.isKey(wifiSSIDKey) || preferences.remove(wifiSSIDKey);
      const bool passRemoved = !preferences.isKey(wifiPassKey) || preferences.remove(wifiPassKey);
      saved = ssidRemoved && passRemoved &&
              !preferences.isKey(wifiSSIDKey) && !preferences.isKey(wifiPassKey);
    }
    if (!saved) {
      if (hadRecord || clearingLegacy) preferences.putBytes("credentials", &previous, sizeof(previous));
      else preferences.remove("credentials");
    }
    preferences.end();
  }
  if (saved) {
    ssid = newSsid;
    pass = newPass;
  } else {
    Serial.println("[prefs] WiFi credential write failed");
  }
  xSemaphoreGive(wifiSettingsMutex);
  return saved;
}

bool WiFiParams::saveMdnsNameForRestart(const char *name, char *stored,
                                      size_t storedSize) {
  init();
  if (!mdnsNameNormalize(name, stored, storedSize)) return false;
  xSemaphoreTake(wifiSettingsMutex, portMAX_DELAY);
  Preferences preferences;
  bool saved = initialized && preferences.begin(wifiPrefsKey, false);
  if (saved) {
    preferences.putString(wifiMdnsNameKey, stored);
    saved = preferences.getString(wifiMdnsNameKey, "\x01") == stored;
    preferences.end();
  }
  xSemaphoreGive(wifiSettingsMutex);
  return saved;
}
#endif
