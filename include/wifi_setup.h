#ifndef WIFI_SETUP
#define WIFI_SETUP

#include <stddef.h>

void setupWifi();
void stopWifi();
void saveCredentials(const String &ssid, const String &pass);
bool saveCredentialsForRestart(const String &ssid, const String &pass);
bool wifiCredentialsSaved();
bool wifiEnsureMdnsReadyForSta();

const char *wifiDeviceName();

bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize);

void wifiSupervise();

extern volatile bool b_wifiEnabled;

extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;
extern const char *wifiMdnsNameKey;

#endif
