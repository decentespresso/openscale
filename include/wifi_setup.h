#ifndef WIFI_SETUP
#define WIFI_SETUP

#include <stddef.h>

// can take a while - check nvs for stored ssid and pass
// check if can connect
// if not, start AP mode
void setupWifi();
void stopWifi();
void saveCredentials(const String &ssid, const String &pass); // ssid, pass
bool saveCredentialsForRestart(const String &ssid, const String &pass);
bool wifiCredentialsSaved();
bool wifiEnsureMdnsReadyForSta();

// Effective device name: the mDNS/DHCP label the scale advertises, default
// "hds" (so an unrenamed scale keeps answering at hds.local). Owned by the boot
// path; safe to read from the main loop and from request handlers.
const char *wifiDeviceName();

// Normalizes name, writes it to NVS, and copies the stored value into stored.
// NVS only: the live mDNS/hostname state is unchanged, so the caller must
// restart the scale for the new identity to take effect. Returns false when the
// name is invalid or the write does not persist.
bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize);

// Periodic health log + STA reconnect supervisor; call once per main-loop pass
// when WiFi is enabled. Recovers from a silent STA disconnect (which the old
// connect-once-at-boot path never did) and prints heap/RSSI/disconnect counts.
// Side effect: contains a heap watchdog that calls esp_restart() if free heap
// stays critically low (<15 KB) for >2 s. While a BLE client is connected the
// reboot is deferred (up to HEAP_CRITICAL_BLE_DEFER_MAX, 60 s) so a live shot
// isn't interrupted, then fires regardless once that window passes.
void wifiSupervise();

extern volatile bool b_wifiEnabled;

extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;
extern const char *wifiMdnsNameKey;

#endif
