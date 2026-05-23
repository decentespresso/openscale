
#include "NetworkEvents.h"
#include "WiFiType.h"
#include "config.h"  // FIRMWARE_VER for the DNS-SD TXT record
#include "esp32-hal.h"
#include "esp_system.h"  // esp_restart() for the heap watchdog
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

bool b_wifiEnabled = false;
// True once we've chosen STA mode (have credentials). Gates the supervisor's
// recovery so it forces STA reconnects even from WL_STOPPED -- where
// WiFi.getMode() no longer reports STA and a getMode()-based check would skip
// recovery, leaving WiFi dead forever (observed after a deauth storm).
static volatile bool g_staIntended = false;

const char *wifiPrefsKey = "wifi";
const char *wifiSSIDKey = "ssid";
const char *wifiPassKey = "pass";

class WiFiParams {
private:
  String ssid = "";
  String pass = "";
  Preferences preferences;

public:
  String getSSID() { return ssid; }
  String getPass() { return pass; }
  bool hasCredentials() { return ssid != ""; };
  void saveCredentials(String ssid, String pass);
  void init();
  void reset();
};

WiFiParams params;

void setupAP() {
  g_staIntended = false;  // AP fallback: don't let the supervisor force STA reconnects
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
  g_staIntended = true;
  WiFi.mode(WIFI_STA);

  WiFi.begin(params.getSSID(), params.getPass());
  WiFi.setTxPower(WIFI_POWER_18_5dBm);
  // Intentionally not calling WiFi.setSleep(false): with BLE active it starves
  // the shared-radio BT/WiFi coexistence layer and causes heavy packet loss.
  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    wifiCounter++;
    delay(1000);
    Serial.println(".");
    // Toggle, to emulate blinking?
    b_wifiEnabled = !b_wifiEnabled;
    if (wifiCounter > 15) {
      // Configured scale: do NOT fall back to AP. The old code called
      // setupAP()+WiFi.disconnect(true) here, which dropped the STA into
      // WL_STOPPED and (via setupAP clearing g_staIntended) disabled recovery,
      // leaving WiFi dead until reboot. Instead leave STA mode and let
      // wifiSupervise() keep (re)connecting in the background.
      Serial.println("WiFi not up yet; continuing to retry in background");
      break;
    }
  }
  Serial.println("");
  Serial.println("Connected to ");
  Serial.println(WiFi.SSID().c_str());
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP().toString().c_str());
  b_wifiEnabled = true;
}

void stopWifi() { WiFi.disconnect(true); }

// ---------------------------------------------------------------------------
// WiFi resilience + instrumentation. The firmware used to connect once at boot
// with no event handler, so a STA deauth -- common under BT/WiFi coexistence --
// left WiFi dead until reboot while the main loop kept running. These add
// disconnect logging (with reason + heap) and automatic recovery.
// ---------------------------------------------------------------------------
static volatile uint32_t g_wifiDisconnects = 0;
static volatile uint32_t g_wifiReconnects = 0;
// Set once the boot-time bring-up (connectToWifi, which runs in the _wifi_init
// task) has finished. wifiSupervise() runs in the main loop concurrently, so it
// must NOT force reconnects while the initial association is still in progress
// or the two race each other into a WL_STOPPED state.
static volatile bool g_wifiInitDone = false;

void setupMdns() {
  if (!MDNS.begin("hds")) {
    Serial.println("could not set up MDNS responder");
    return;
  }
  // Friendly instance name + DNS-SD service so apps (incl. Android NsdManager)
  // can discover the scale; a bare hds.local A record isn't browsable there.
  MDNS.setInstanceName("Half Decent Scale");
  MDNS.addService("decentscale", "tcp", 80);
  MDNS.addServiceTxt("decentscale", "tcp", "fw", (const char *)FIRMWARE_VER);
  MDNS.addServiceTxt("decentscale", "tcp", "model", "hds");
  MDNS.addServiceTxt("decentscale", "tcp", "proto", "ws");
  MDNS.addServiceTxt("decentscale", "tcp", "path", "/snapshot");
  Serial.println("DNS-SD: advertised _decentscale._tcp on port 80");
}

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
      // Re-advertise mDNS after a (re)connect so hds.local keeps resolving.
      MDNS.end();
      setupMdns();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
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

  const char *hostname = "hds.local";
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname);

  // Register the handler BEFORE connecting so connect/disconnect/GOT_IP are all
  // logged. Disable the core's fixed ~4 s auto-reconnect: when an AP is
  // rate-limiting us (deauth storm), retrying every few seconds perpetuates the
  // block. wifiSupervise() retries with exponential backoff (quiet gaps) so the
  // AP can recover.
  WiFi.onEvent(onWifiEvent);
  WiFi.setAutoReconnect(false);

  if (params.hasCredentials()) {
    Serial.printf("trying to connect to wifi: %s\n", params.getSSID());
    connectToWifi();
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
  }

  // STA mode also advertises mDNS from the GOT_IP handler; calling it here as
  // well covers AP fallback and is harmless to repeat.
  setupMdns();

  // Bring-up complete: from here the loop's supervisor may manage reconnects.
  g_wifiInitDone = true;
}

void wifiSupervise() {
  static unsigned long lastLog = 0;
  static unsigned long downSince = 0;
  static unsigned long lastAttempt = 0;
  static unsigned long backoffMs = 0;
  static unsigned long lowHeapSince = 0;
  unsigned long now = millis();
  bool up = WiFi.status() == WL_CONNECTED;

  // Heap watchdog. Connection churn can drain the heap to near-zero; in that
  // OOM window the lwIP/IP stack wedges permanently (no ICMP/TCP/mDNS) while
  // WiFi stays associated and the main loop runs -- so WiFi.status() can't see
  // it. If free heap stays critically low for a sustained window, reboot to
  // self-heal (clients auto-reconnect); a reboot is far better than the silent
  // permanent hang it replaces. Normal idle heap is ~70 KB, so this only fires
  // under pathological churn, not in everyday use.
  uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t HEAP_CRITICAL = 15000;
  const unsigned long HEAP_CRITICAL_WINDOW = 2000;
  if (freeHeap < HEAP_CRITICAL) {
    if (lowHeapSince == 0) {
      lowHeapSince = now;
      Serial.printf("[heap] CRITICAL low free=%lu minfree=%lu @%lu\n",
                    (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(), now);
    } else if (now - lowHeapSince >= HEAP_CRITICAL_WINDOW) {
      Serial.printf("[heap] critical for %lums (free=%lu) -> esp_restart()\n",
                    now - lowHeapSince, (unsigned long)freeHeap);
      Serial.flush();
      esp_restart();
    }
  } else if (freeHeap > HEAP_CRITICAL + 5000) {  // hysteresis so it doesn't flap
    lowHeapSince = 0;
  }

  if (now - lastLog >= 5000) {
    lastLog = now;
    Serial.printf("[health] uptime=%lu wifi_status=%d rssi=%d heap=%lu minheap=%lu disc=%lu rec=%lu\n",
                  now, (int)WiFi.status(), up ? (int)WiFi.RSSI() : 0,
                  (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                  (unsigned long)g_wifiDisconnects, (unsigned long)g_wifiReconnects);
  }

  // Reconnect supervisor with exponential backoff. Gated on having credentials
  // (NOT WiFi.getMode() or a one-shot flag) so it always recovers a configured
  // scale -- including from WL_STOPPED, where getMode() no longer reports STA.
  // Backoff (5 s -> 10 -> 20 -> 40 -> 60 cap, with a quiet WiFi.disconnect()
  // between tries) keeps a rate-limiting AP from being hammered into holding the
  // deauth block, while still recovering a normal one-off deauth within ~5 s.
  if (g_wifiInitDone && params.hasCredentials() && !up) {
    if (downSince == 0) {
      downSince = now;
      backoffMs = 5000;            // first retry is quick (normal deauth recovery)
      lastAttempt = now - backoffMs;  // ...so the branch below fires next tick
      Serial.printf("[wifi] link down @%lu status=%d\n", now, (int)WiFi.status());
    }
    if (now - lastAttempt >= backoffMs) {
      g_wifiReconnects++;
      lastAttempt = now;
      Serial.printf("[wifi] down %lums (status=%d) -> reconnect #%lu backoff=%lums heap=%lu\n",
                    now - downSince, (int)WiFi.status(), (unsigned long)g_wifiReconnects,
                    backoffMs, (unsigned long)ESP.getFreeHeap());
      // Go quiet first (stop any in-flight assoc), then one clean attempt.
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(params.getSSID(), params.getPass());
      backoffMs = backoffMs < 60000 ? backoffMs * 2 : 60000;  // escalate, cap 60 s
    }
  } else if (up) {
    downSince = 0;
    backoffMs = 0;
  }
}

void saveCredentials(String ssid, String pass) {
  params.saveCredentials(ssid, pass);
}

// ----------------------------------------------------
// ------------------ WiFiParams ----------------------
// ----------------------------------------------------

void WiFiParams::saveCredentials(String ssid, String pass) {
  if (this->ssid == ssid && this->pass == pass)
    return;

  this->ssid = ssid;
  this->pass = pass;
  preferences.putString("ssid", ssid.c_str());
  preferences.putString("pass", pass.c_str());
}

void WiFiParams::init() {
  preferences.begin(wifiPrefsKey);
  if (!hasCredentials()) {
    this->ssid = preferences.getString(wifiSSIDKey, "");
    this->pass = preferences.getString(wifiPassKey, "");
  }
}

void WiFiParams::reset() {
  ssid = "";
  pass = "";
  preferences.clear();
}
