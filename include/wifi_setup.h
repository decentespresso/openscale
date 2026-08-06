#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "config.h"

#if HDS_FEATURE_WIFI

#include <Arduino.h>
#include <stddef.h>

void setupWifi();
void stopWifi();
void saveCredentials(const String &ssid, const String &pass);
bool saveCredentialsForRestart(const String &ssid, const String &pass);
bool wifiCredentialsSaved();
#if HDS_FEATURE_MDNS
bool wifiEnsureMdnsReadyForSta();
#endif
const char *wifiDeviceName();
bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize);
void wifiSupervise();
void wifi_init();

extern volatile bool b_wifiEnabled;
extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;
extern const char *wifiMdnsNameKey;

#endif

#endif
