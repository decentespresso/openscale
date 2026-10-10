#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include <Preferences.h>
#include "wifi_setup.h"
#include "wifi_settings.h"

static bool wifiCredentialRecordMatches(Preferences &preferences,
                                        const WifiCredentials &record) {
  WifiCredentials stored;
  return preferences.getBytesLength("credentials") == sizeof(stored) &&
         preferences.getBytes("credentials", &stored, sizeof(stored)) == sizeof(stored) &&
         memcmp(&record, &stored, sizeof(record)) == 0;
}

static bool wifiWriteCredentialRecord(Preferences &preferences,
                                      const WifiCredentials &record) {
  return preferences.putBytes("credentials", &record, sizeof(record)) == sizeof(record) &&
         wifiCredentialRecordMatches(preferences, record);
}

static bool wifiLegacyCredentialsMatch(Preferences &preferences,
                                      const WifiCredentials &record) {
  if (record.ssid[0] == 0) {
    return !preferences.isKey(wifiSSIDKey) && !preferences.isKey(wifiPassKey);
  }
  return preferences.isKey(wifiSSIDKey) && preferences.isKey(wifiPassKey) &&
         preferences.getString(wifiSSIDKey, "") == record.ssid &&
         preferences.getString(wifiPassKey, "") == record.pass;
}

static bool wifiWriteLegacyCredentials(Preferences &preferences,
                                      const WifiCredentials &record) {
  if (wifiLegacyCredentialsMatch(preferences, record)) return true;
  if (record.ssid[0] == 0) {
    const bool ssidRemoved = !preferences.isKey(wifiSSIDKey) || preferences.remove(wifiSSIDKey);
    const bool passRemoved = !preferences.isKey(wifiPassKey) || preferences.remove(wifiPassKey);
    return ssidRemoved && passRemoved && wifiLegacyCredentialsMatch(preferences, record);
  }
  preferences.putString(wifiSSIDKey, record.ssid);
  preferences.putString(wifiPassKey, record.pass);
  return wifiLegacyCredentialsMatch(preferences, record);
}

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
        if (!wifiWriteLegacyCredentials(preferences, record)) {
          Serial.println("[prefs] WiFi legacy credential repair failed");
        }
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
  if (!initialized || newSsid.length() > 32 || newPass.length() > 64 ||
      ssid.length() > 32 || pass.length() > 64) {
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
    memcpy(previous.ssid, ssid.c_str(), ssid.length() + 1);
    memcpy(previous.pass, pass.c_str(), pass.length() + 1);
    saved = wifiCredentialRecordMatches(preferences, previous) ||
            wifiWriteCredentialRecord(preferences, previous);
    if (saved) {
      saved = wifiWriteLegacyCredentials(preferences, record) &&
              wifiWriteCredentialRecord(preferences, record);
      if (!saved) {
        const bool recordRestored = wifiCredentialRecordMatches(preferences, previous) ||
                                    wifiWriteCredentialRecord(preferences, previous);
        const bool legacyRestored = wifiWriteLegacyCredentials(preferences, previous);
        if (!recordRestored || !legacyRestored) {
          Serial.println("[prefs] WiFi credential restore failed");
        }
      }
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

bool WiFiParams::prepareLegacyDowngrade() {
  init();
  xSemaphoreTake(wifiSettingsMutex, portMAX_DELAY);
  if (!initialized || ssid.length() > 32 || pass.length() > 64) {
    xSemaphoreGive(wifiSettingsMutex);
    return false;
  }
  WifiCredentials record;
  memcpy(record.ssid, ssid.c_str(), ssid.length() + 1);
  memcpy(record.pass, pass.c_str(), pass.length() + 1);
  Preferences preferences;
  bool prepared = preferences.begin(wifiPrefsKey, false);
  if (prepared) {
    if (preferences.isKey("credentials") ||
        !wifiLegacyCredentialsMatch(preferences, record)) {
      prepared = (wifiCredentialRecordMatches(preferences, record) ||
                  wifiWriteCredentialRecord(preferences, record)) &&
                 wifiWriteLegacyCredentials(preferences, record);
      if (prepared) {
        prepared = preferences.remove("credentials") && !preferences.isKey("credentials");
      }
    }
    preferences.end();
  }
  xSemaphoreGive(wifiSettingsMutex);
  return prepared;
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
