#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include "NetworkEvents.h"
#include "WiFiType.h"
#include "config.h"  // FIRMWARE_VER for the DNS-SD TXT record
#include "esp32-hal.h"
#include "esp_system.h"  // esp_restart() for the heap watchdog
#include "mdns_name.h"
#include "timing.h"
#include <Arduino.h>
#include "wifi_setup.h"
#include "wifi_settings.h"
#if HDS_FEATURE_MDNS
#include <ESPmDNS.h>
#endif
#include <Preferences.h>
#include <WiFi.h>
#if HDS_ENABLE_ENERGY_MENU
#include "energy_policy.h"

extern EnergyPolicy energyPolicy;
#endif

volatile bool b_wifiEnabled = false;
#if HDS_FEATURE_MDNS
static volatile bool g_mdnsAdvertisePending = false;
static bool g_mdnsReady = false;
static const unsigned long MDNS_GOODBYE_DRAIN_MS = 60;
#endif
extern volatile bool deviceConnected;
extern volatile bool b_ota;

const char *wifiPrefsKey = "wifi";
const char *wifiSSIDKey = "ssid";
const char *wifiPassKey = "pass";
const char *wifiMdnsNameKey = "mdns_name";

WiFiParams params;

static void mdnsWithdraw();
#if HDS_FEATURE_MDNS
bool setupMdns();
#endif

void setupAP() {
  mdnsWithdraw();
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP);
  delay(100);
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1),
                    IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP("DecentScale", "12345678")) {
    Serial.println("[wifi] access point startup failed");
  }
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  Serial.println("WiFi: DecentScale");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  b_wifiEnabled = true;
#if HDS_FEATURE_MDNS
  setupMdns();
#endif
}

void wifiStartStation(const char *ssid, const char *pass) {
  mdnsWithdraw();
  if (WiFi.getMode() & WIFI_AP) WiFi.softAPdisconnect(false);
  if (!WiFi.disconnect(false, false, 1000)) WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  portENTER_CRITICAL(&wifiSetupMux);
  wifiSetupRuntime.ipBaseline = wifiGotIpGeneration;
  wifiDisconnectReason = 0;
  wifiStaAssociated = false;
  portEXIT_CRITICAL(&wifiSetupMux);
  WiFi.setMinSecurity(pass[0] == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK);
  WiFi.begin(ssid, pass);
  wifiSetupRuntime.stationStartedAt = millis();
  WiFi.setTxPower(WIFI_POWER_18_5dBm);
  b_wifiEnabled = true;
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
  wifiInitLocks();
  portENTER_CRITICAL(&wifiSetupMux);
  wifiWorkerStopRequested = true;
  wifiWorkerStartRequested = false;
  portEXIT_CRITICAL(&wifiSetupMux);
  xSemaphoreTake(wifiRadioMutex, portMAX_DELAY);
  wifiCancelSetup();
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) {
    mdnsWithdraw();
    b_wifiEnabled = false;
    wifiPublishSetupStatus();
    xSemaphoreGive(wifiRadioMutex);
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
  wifiPublishSetupStatus();
  xSemaphoreGive(wifiRadioMutex);
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
  wifiInitLocks();
  xSemaphoreTake(wifiRadioMutex, portMAX_DELAY);
  if (WiFi.status() != WL_CONNECTED || (uint32_t)WiFi.localIP() == 0) {
    Serial.printf("[wifi] MDNS not ready wifi=%d ip=%s\n",
                  (int)WiFi.status(),
                  WiFi.localIP().toString().c_str());
    g_mdnsReady = false;
    xSemaphoreGive(wifiRadioMutex);
    return false;
  }
  if (g_mdnsReady) {
    xSemaphoreGive(wifiRadioMutex);
    return true;
  }
  MDNS.end();
  g_mdnsReady = false;
  const bool ready = setupMdns();
  xSemaphoreGive(wifiRadioMutex);
  return ready;
}
#endif

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      portENTER_CRITICAL(&wifiSetupMux);
      wifiStaAssociated = true;
      portEXIT_CRITICAL(&wifiSetupMux);
      Serial.printf("[wifi] STA connected ch=%u heap=%lu\n",
                    info.wifi_sta_connected.channel,
                    (unsigned long)ESP.getFreeHeap());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      portENTER_CRITICAL(&wifiSetupMux);
      wifiGotIpGeneration = wifiGotIpGeneration + 1;
      portEXIT_CRITICAL(&wifiSetupMux);
      Serial.printf("[wifi] GOT_IP %s heap=%lu\n",
                    WiFi.localIP().toString().c_str(),
                    (unsigned long)ESP.getFreeHeap());
#if HDS_FEATURE_MDNS
      g_mdnsAdvertisePending = true;
#endif
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      portENTER_CRITICAL(&wifiSetupMux);
      wifiDisconnectGeneration = wifiDisconnectGeneration + 1;
      wifiDisconnectReason = info.wifi_sta_disconnected.reason;
      wifiStaAssociated = false;
      portEXIT_CRITICAL(&wifiSetupMux);
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
  wifiInitLocks();
  xSemaphoreTake(wifiRadioMutex, portMAX_DELAY);
  if (wifiWorkerStopRequested) {
    xSemaphoreGive(wifiRadioMutex);
    return;
  }
  params.init();

  WiFi.setHostname(params.getMdnsName());
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);

  static bool eventsRegistered = false;
  if (!eventsRegistered) {
    WiFi.onEvent(onWifiEvent);
    eventsRegistered = true;
  }
  WiFi.setAutoReconnect(false);

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.persistent(false);

  if (params.hasCredentials()) {
    Serial.printf("trying to connect to wifi: %s\n", params.getSSID().c_str());
    wifiStartStation(params.getSSID().c_str(), params.getPass().c_str());
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
  }

  g_wifiInitDone = true;
  wifiPublishSetupStatus();
  xSemaphoreGive(wifiRadioMutex);
}

void wifiSupervise() {
  if (xTaskGetCurrentTaskHandle() != wifiWorkerTask) return;
  static unsigned long lastRun = 0;
  static unsigned long lastLog = 0;
  static unsigned long downSince = 0;
  static unsigned long lastAttempt = 0;
  static unsigned long backoffMs = 0;
  static unsigned long lowHeapSince = 0;
  static unsigned long lastDeferLog = 0;
  unsigned long now = millis();
  if (!hdsIntervalElapsed(now, lastRun, WIFI_SUPERVISE_INTERVAL_MS)) {
    return;
  }
  lastRun = now;
  xSemaphoreTake(wifiRadioMutex, portMAX_DELAY);
  if (!b_wifiEnabled) {
    downSince = 0;
    backoffMs = 0;
    xSemaphoreGive(wifiRadioMutex);
    return;
  }
  if (b_ota) {
    xSemaphoreGive(wifiRadioMutex);
    return;
  }
  wifiProcessSetup();
  now = millis();
  bool up = WiFi.status() == WL_CONNECTED;
  wifiPublishSetupStatus();

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

  if (g_wifiInitDone && params.hasCredentials() && !up &&
      WiFi.getMode() == WIFI_STA && !wifiSetupBusy()) {
    if (downSince == 0 && now - wifiSetupRuntime.stationStartedAt < 20000) {
      xSemaphoreGive(wifiRadioMutex);
      return;
    }
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
      wifiStartStation(params.getSSID().c_str(), params.getPass().c_str());
      backoffMs = backoffMs < 60000 ? backoffMs * 2 : 60000;
    }
  } else {
    downSince = 0;
    backoffMs = 0;
  }
  xSemaphoreGive(wifiRadioMutex);
}

bool saveCredentials(const String &ssid, const String &pass) {
  wifiInitLocks();
  xSemaphoreTake(wifiRadioMutex, portMAX_DELAY);
  wifiCancelSetup();
  const bool saved = params.saveCredentials(ssid, pass);
  wifiPublishSetupStatus();
  xSemaphoreGive(wifiRadioMutex);
  return saved;
}

bool wifiCredentialsSaved() {
  params.init();
  xSemaphoreTake(wifiSettingsMutex, portMAX_DELAY);
  const bool saved = params.hasCredentials();
  xSemaphoreGive(wifiSettingsMutex);
  return saved;
}

const char *wifiDeviceName() {
  params.init();
  return params.getMdnsName();
}

bool saveDeviceNameForRestart(const char *name, char *stored, size_t storedSize) {
  return params.saveMdnsNameForRestart(name, stored, storedSize);
}


#endif
