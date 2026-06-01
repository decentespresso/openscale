#ifndef WIFI_SETUP
#define WIFI_SETUP

// can take a while - check nvs for stored ssid and pass
// check if can connect
// if not, start AP mode
void setupWifi();
void stopWifi();
void saveCredentials(String ssid, String pass); // ssid, pass
bool wifiCredentialsSaved();

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

#endif
