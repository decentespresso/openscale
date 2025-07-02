#ifndef WIFI_SETUP
#define WIFI_SETUP

// can take a while - check nvs for stored ssid and pass
// check if can connect
// if not, start AP mode
void setupWifi();
void saveCredentials(String ssid, String pass); // ssid, pass

extern const char *wifiPrefsKey;
extern const char *wifiSSIDKey;
extern const char *wifiPassKey;

#endif
