#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include "NetworkEvents.h"
#include "WiFiType.h"
#include "config.h"  // FIRMWARE_VER for the DNS-SD TXT record
#include "esp32-hal.h"
#include "esp_system.h"  // esp_restart() for the heap watchdog
#include "mdns_name.h"
#include <Arduino.h>
#if HDS_FEATURE_MDNS
#include <ESPmDNS.h>
#endif
#include <Preferences.h>
#include <WiFi.h>

volatile bool b_wifiEnabled = false;
#if HDS_FEATURE_MDNS
static volatile bool g_mdnsAdvertisePending = false;
static bool g_mdnsReady = false;
static const unsigned long MDNS_GOODBYE_DRAIN_MS = 60;
#endif
extern volatile bool deviceConnected;

const char *wifiPrefsKey = "wifi";
const char *wifiSSIDKey = "ssid";
const char *wifiPassKey = "pass";
const char *wifiMdnsNameKey = "mdns_name";

class WiFiParams {
private:
  String ssid = "";
  String pass = "";
  char mdnsName[MDNS_NAME_BUFFER_BYTES] = {0};
  Preferences preferences;
  bool initialized = false;
  bool writeCredentialsToNvs(const String &ssid, const String &pass);

public:
  const String &getSSID() const { return ssid; }
  const String &getPass() const { return pass; }
  const char *getMdnsName() const {
    return mdnsName[0] != 0 ? mdnsName : mdnsNameDefault();
  }
  bool hasCredentials() const { return ssid != ""; };
  void saveCredentials(const String &ssid, const String &pass);
  bool saveCredentialsForRestart(const String &ssid, const String &pass);
  bool saveMdnsNameForRestart(const char *name, char *stored, size_t storedSize);
  void init();
  void reset();
};

WiFiParams params;

void setupAP() {
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAP("DecentScale", "12345678");
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1),
                    IPAddress(255, 255, 255, 0));

  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  WiFi.printDiag(Serial);
  Serial.println("WiFi: DecentScale");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  b_wifiEnabled = true;
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(params.getSSID(), params.getPass());
  WiFi.setTxPower(WIFI_POWER_18_5dBm);
  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    wifiCounter++;
    delay(1000);
    Serial.println(".");
    if (wifiCounter > 15) {
      Serial.println("WiFi not up yet; continuing to retry in background");
      break;
    }
  }
  b_wifiEnabled = true;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID().c_str());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP().toString().c_str());
  }
}

#if HDS_FEATURE_MDNS
static void mdnsWithdraw() {
  if (!g_mdnsReady) {
    return;
  }
  MDNS.end();
  g_mdnsReady = false;
  g_mdnsAdvertisePending = false;
  delay(MDNS_GOODBYE_DRAIN_MS);
}
#else
static void mdnsWithdraw() {}
#endif

void stopWifi() {
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) {
    mdnsWithdraw();
    b_wifiEnabled = false;
    return;
  }

  mdnsWithdraw();
  if ((mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false);
  }
  if (mode & WIFI_MODE_AP) {
    WiFi.softAPdisconnect(false);
  }
  WiFi.mode(WIFI_OFF);
  b_wifiEnabled = false;
}

static volatile uint32_t g_wifiDisconnects = 0;
static volatile uint32_t g_wifiReconnects = 0;
static volatile bool g_wifiInitDone = false;

#if HDS_FEATURE_MDNS
bool setupMdns() {
  if (WiFi.getMode() != WIFI_AP && WiFi.status() != WL_CONNECTED) {
    Serial.printf("[wifi] MDNS deferred wifi=%d ip=%s\n",
                  (int)WiFi.status(),
                  WiFi.localIP().toString().c_str());
    g_mdnsReady = false;
    return false;
  }
  const char *name = params.getMdnsName();
  if (!MDNS.begin(name)) {
    Serial.println("could not set up MDNS responder");
    g_mdnsReady = false;
    return false;
  }
  if (mdnsNameIsDefault(name)) {
    MDNS.setInstanceName("Half Decent Scale");
  } else {
    char instance[MDNS_NAME_BUFFER_BYTES + 24];
    snprintf(instance, sizeof(instance), "Half Decent Scale (%s)", name);
    MDNS.setInstanceName(instance);
  }
  MDNS.addService("decentscale", "tcp", 80);
  MDNS.addServiceTxt("decentscale", "tcp", "fw", (const char *)FIRMWARE_VER);
  MDNS.addServiceTxt("decentscale", "tcp", "model", "hds");
  MDNS.addServiceTxt("decentscale", "tcp", "name", name);
#if HDS_FEATURE_WEBSOCKET
  MDNS.addServiceTxt("decentscale", "tcp", "proto", "ws");
  MDNS.addServiceTxt("decentscale", "tcp", "path", "/snapshot");
#endif
  Serial.printf("DNS-SD: advertised %s.local _decentscale._tcp on port 80\n", name);
  g_mdnsReady = true;
  return true;
}

bool wifiEnsureMdnsReadyForSta() {
  if (WiFi.status() != WL_CONNECTED || (uint32_t)WiFi.localIP() == 0) {
    Serial.printf("[wifi] MDNS not ready wifi=%d ip=%s\n",
                  (int)WiFi.status(),
                  WiFi.localIP().toString().c_str());
    g_mdnsReady = false;
    return false;
  }
  if (g_mdnsReady) {
    return true;
  }
  MDNS.end();
  g_mdnsReady = false;
  return setupMdns();
}
#endif

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.printf("[wifi] STA connected ch=%u heap=%lu\n",
                    info.wifi_sta_connected.channel,
                    (unsigned long)ESP.getFreeHeap());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] GOT_IP %s heap=%lu\n",
                    WiFi.localIP().toString().c_str(),
                    (unsigned long)ESP.getFreeHeap());
#if HDS_FEATURE_MDNS
      g_mdnsAdvertisePending = true;
#endif
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
#if HDS_FEATURE_MDNS
      g_mdnsReady = false;
#endif
      g_wifiDisconnects++;
      Serial.printf("[wifi] *** STA DISCONNECTED #%lu reason=%u heap=%lu minheap=%lu uptime=%lu\n",
                    (unsigned long)g_wifiDisconnects,
                    info.wifi_sta_disconnected.reason,
                    (unsigned long)ESP.getFreeHeap(),
                    (unsigned long)ESP.getMinFreeHeap(),
                    (unsigned long)millis());
      break;
    default:
      break;
  }
}

void setupWifi() {
  params.init();

  WiFi.setHostname(params.getMdnsName());
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);

  WiFi.onEvent(onWifiEvent);
  WiFi.setAutoReconnect(false);

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.persistent(false);

  if (params.hasCredentials()) {
    Serial.printf("trying to connect to wifi: %s\n", params.getSSID().c_str());
    connectToWifi();
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
#if HDS_FEATURE_MDNS
    setupMdns();
#endif
  }

  g_wifiInitDone = true;
}

void wifiSupervise() {
  static unsigned long lastLog = 0;
  static unsigned long downSince = 0;
  static unsigned long lastAttempt = 0;
  static unsigned long backoffMs = 0;
  static unsigned long lowHeapSince = 0;
  static unsigned long lastDeferLog = 0;
  unsigned long now = millis();
  bool up = WiFi.status() == WL_CONNECTED;

#if HDS_FEATURE_MDNS
  if (g_mdnsAdvertisePending) {
    g_mdnsAdvertisePending = false;
    MDNS.end();
    g_mdnsReady = false;
    setupMdns();
  }
#endif

  uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t HEAP_CRITICAL = 15000;
  const unsigned long HEAP_CRITICAL_WINDOW = 2000;
  const unsigned long HEAP_CRITICAL_BLE_DEFER_MAX = 60000;
  if (freeHeap < HEAP_CRITICAL) {
    if (lowHeapSince == 0) {
      lowHeapSince = now;
      Serial.printf("[heap] CRITICAL low free=%lu minfree=%lu @%lu\n",
                    (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(), now);
    } else if (now - lowHeapSince >= HEAP_CRITICAL_WINDOW) {
      if (deviceConnected && now - lowHeapSince < HEAP_CRITICAL_BLE_DEFER_MAX) {
        if (now - lastDeferLog >= 5000) {
          lastDeferLog = now;
          Serial.printf("[heap] critical for %lums (free=%lu) but BLE connected -> defer reboot\n",
                        now - lowHeapSince, (unsigned long)freeHeap);
        }
      } else {
        Serial.printf("[heap] critical for %lums (free=%lu) -> esp_restart()\n",
                      now - lowHeapSince, (unsigned long)freeHeap);
        Serial.flush();
        esp_restart();
      }
    }
  } else {
    lowHeapSince = 0;
    lastDeferLog = 0;
  }

  if (now - lastLog >= 5000) {
    lastLog = now;
    Serial.printf("[health] uptime=%lu wifi_status=%d rssi=%d heap=%lu minheap=%lu disc=%lu rec=%lu\n",
                  now, (int)WiFi.status(), up ? (int)WiFi.RSSI() : 0,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                  (unsigned long)g_wifiDisconnects, (unsigned long)g_wifiReconnects);
  }

  if (g_wifiInitDone && params.hasCredentials() && !up) {
    if (downSince == 0) {
      downSince = now;
      backoffMs = 5000;
      lastAttempt = now - backoffMs;
      Serial.printf("[wifi] link down @%lu status=%d\n", now, (int)WiFi.status());
    }
    if (now - lastAttempt >= backoffMs) {
      g_wifiReconnects++;
      lastAttempt = now;
      Serial.printf("[wifi] down %lums (status=%d) -> reconnect #%lu backoff=%lums heap=%lu\n",
                    now - downSince, (int)WiFi.status(), (unsigned long)g_wifiReconnects,
                    backoffMs, (unsigned long)ESP.getFreeHeap());
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(params.getSSID(), params.getPass());
      backoffMs = backoffMs < 60000 ? backoffMs * 2 : 60000;
    }
  } else if (up) {
    downSince = 0;
    backoffMs = 0;
  }
}

void saveCredentials(const String &ssid, const String &pass) {
  params.saveCredentials(ssid, pass);
}

bool saveCredentialsForRestart(const String &ssid, const String &pass) {
  return params.saveCredentialsForRestart(ssid, pass);
}

bool wifiCredentialsSaved() {
  params.init();
  return params.hasCredentials();
}

const char *wifiDeviceName() {
  params.init();
  return params.getMdnsName();
}

bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize) {
  return params.saveMdnsNameForRestart(name, stored, storedSize);
}


void WiFiParams::saveCredentials(const String &ssid, const String &pass) {
  if (!initialized) {
    init();
  }
  if (!initialized) {
    Serial.println("[prefs] could not save credentials -- NVS namespace unavailable");
    return;
  }

  if (this->ssid == ssid && this->pass == pass)
    return;

  this->ssid = ssid;
  this->pass = pass;
  writeCredentialsToNvs(ssid, pass);
}

bool WiFiParams::saveCredentialsForRestart(const String &ssid, const String &pass) {
  if (!initialized) {
    init();
  }
  if (!initialized) {
    Serial.println("[prefs] could not save credentials -- NVS namespace unavailable");
    return false;
  }

  return writeCredentialsToNvs(ssid, pass);
}

bool WiFiParams::writeCredentialsToNvs(const String &ssid, const String &pass) {
  size_t wroteSsid = preferences.putString(wifiSSIDKey, ssid.c_str());
  size_t wrotePass = preferences.putString(wifiPassKey, pass.c_str());
  String storedSsid = preferences.getString(wifiSSIDKey, "\x01");
  String storedPass = preferences.getString(wifiPassKey, "\x01");
  bool saved = storedSsid == ssid && storedPass == pass;
  if (!saved) {
    Serial.printf("[prefs] NVS write FAILED (ssid=%u pass=%u) -- credentials did not persist\n",
                  (unsigned)wroteSsid, (unsigned)wrotePass);
  }
  return saved;
}

bool WiFiParams::saveMdnsNameForRestart(const char *name, char *stored, size_t storedSize) {
  if (!initialized) {
    init();
  }
  if (!initialized) {
    Serial.println("[prefs] could not save device name -- NVS namespace unavailable");
    return false;
  }
  if (!mdnsNameNormalize(name, stored, storedSize)) {
    return false;
  }

  preferences.putString(wifiMdnsNameKey, stored);
  return preferences.getString(wifiMdnsNameKey, "\x01") == stored;
}

void WiFiParams::init() {
  if (initialized) {
    return;
  }
  if (!preferences.begin(wifiPrefsKey)) {
    Serial.println("[prefs] could not open NVS namespace 'wifi' -- no stored credentials");
    return;
  }
  initialized = true;
  if (!hasCredentials()) {
    this->ssid = preferences.getString(wifiSSIDKey, "");
    this->pass = preferences.getString(wifiPassKey, "");
  }
  if (mdnsName[0] == 0) {
    char storedName[MDNS_NAME_BUFFER_BYTES] = {0};
    size_t length = preferences.getString(wifiMdnsNameKey, storedName, sizeof(storedName));
    if (length == 0 || !mdnsNameNormalize(storedName, mdnsName, sizeof(mdnsName))) {
      mdnsNameCopyDefault(mdnsName, sizeof(mdnsName));
    }
  }
}

void WiFiParams::reset() {
  ssid = "";
  pass = "";
  if (!initialized) {
    init();
  }
  if (initialized) {
    preferences.clear();
  }
  mdnsNameCopyDefault(mdnsName, sizeof(mdnsName));
}

#endif
