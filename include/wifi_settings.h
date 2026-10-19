#ifndef WIFI_SETTINGS_H
#define WIFI_SETTINGS_H

#include <Arduino.h>
#include "mdns_name.h"

class WiFiParams {
private:
  String ssid;
  String pass;
  char mdnsName[MDNS_NAME_BUFFER_BYTES] = {};
  bool initialized = false;

public:
  const String &getSSID() const { return ssid; }
  const String &getPass() const { return pass; }
  const char *getMdnsName() const {
    return mdnsName[0] != 0 ? mdnsName : mdnsNameDefault();
  }
  bool hasCredentials() const { return ssid.length() != 0; }
  bool saveCredentials(const String &ssid, const String &pass);
  bool prepareLegacyDowngrade();
  bool saveMdnsNameForRestart(const char *name, char *stored, size_t storedSize);
  void init();
};

extern WiFiParams params;

#endif
