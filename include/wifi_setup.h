#ifndef WIFI_SETUP
#define WIFI_SETUP

// can take a while - check nvs for stored ssid and pass
// check if can connect
// if not, start AP mode
void setupWifi();
void stopWifi();
void saveCredentials(String ssid, String pass); // ssid, pass

// Periodic health log + STA reconnect supervisor; call once per main-loop pass
// when WiFi is enabled. Recovers from a silent STA disconnect (which the old
// connect-once-at-boot path never did) and prints heap/RSSI/disconnect counts.
void wifiSupervise();

extern bool b_wifiEnabled;

extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;

#endif
