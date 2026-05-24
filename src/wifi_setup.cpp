
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
// Set by the GOT_IP WiFi event; the main loop (wifiSupervise) consumes it and
// (re)advertises mDNS, keeping all mDNS work off the WiFi-event task.
static volatile bool g_mdnsAdvertisePending = false;
// BLE link state (defined in declare.h). Used by the heap watchdog to avoid
// rebooting mid-shot while a BLE client is connected. volatile: written from
// the BLE task, read here on the main loop.
extern volatile bool deviceConnected;

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
  // Intentionally not calling WiFi.setSleep(false): with BLE active it starves
  // the shared-radio BT/WiFi coexistence layer and causes heavy packet loss.
  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    wifiCounter++;
    delay(1000);
    Serial.println(".");
    if (wifiCounter > 15) {
      // Configured scale: do NOT fall back to AP. The old code called
      // setupAP()+WiFi.disconnect(true) here, which dropped the STA into
      // WL_STOPPED and disabled recovery, leaving WiFi dead until reboot.
      // Instead leave STA mode and let wifiSupervise() keep (re)connecting in
      // the background.
      Serial.println("WiFi not up yet; continuing to retry in background");
      break;
    }
  }
  // b_wifiEnabled means "WiFi subsystem active" (already true from _wifi_init),
  // NOT "connected" -- the live link state is WiFi.status() and the supervisor
  // owns reconnects. Only log success when actually connected (the background-
  // retry path breaks out while still disconnected).
  b_wifiEnabled = true;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID().c_str());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP().toString().c_str());
  }
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
// Set once the boot-time bring-up has finished. That bring-up is setupWifi()
// (which calls connectToWifi()), run from the _wifi_init FreeRTOS task.
// wifiSupervise() runs in the main loop concurrently, so it must NOT force
// reconnects while the initial association is still in progress, or the two
// race each other into a WL_STOPPED state.
static volatile bool g_wifiInitDone = false;

// Advertise hds.local + the _decentscale._tcp DNS-SD service. Returns true if
// the responder came up; callers may log/branch on failure (MDNS.begin can
// transiently fail under heap pressure at reconnect time).
bool setupMdns() {
  if (!MDNS.begin("hds")) {
    Serial.println("could not set up MDNS responder");
    return false;
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
  return true;
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
      // Defer the mDNS (re)advertise to the main loop (wifiSupervise). Running
      // MDNS.end()/begin() from this WiFi-event-task context races the main
      // loop's mDNS/web-server use; the flag keeps all mDNS work on one thread
      // and avoids double-registering the service on boot.
      g_mdnsAdvertisePending = true;
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
    // STA advertises mDNS via the GOT_IP path (deferred to wifiSupervise), so
    // don't also register it here -- that would double-register the service.
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
    // AP mode emits no GOT_IP event, so advertise mDNS directly.
    setupMdns();
  }

  // Bring-up complete: from here the loop's supervisor may manage reconnects.
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

  // (Re)advertise mDNS here -- on the main loop -- when the GOT_IP event asked
  // for it, instead of doing MDNS.end()/begin() from the WiFi-event task.
  // Clear the flag BEFORE the work: if a later GOT_IP fires while setupMdns() is
  // running, it re-sets the flag and we re-advertise on the next pass (so a
  // reconnect during the rebuild is never lost).
  if (g_mdnsAdvertisePending) {
    g_mdnsAdvertisePending = false;
    MDNS.end();
    setupMdns();
  }

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
  // Upper bound on how long we'll defer the reboot for a connected BLE client.
  // A normal shot is well under this, so we never reboot mid-shot, but a stack
  // that stays wedged this long still self-heals even if BLE never disconnects.
  const unsigned long HEAP_CRITICAL_BLE_DEFER_MAX = 60000;
  if (freeHeap < HEAP_CRITICAL) {
    if (lowHeapSince == 0) {
      lowHeapSince = now;
      Serial.printf("[heap] CRITICAL low free=%lu minfree=%lu @%lu\n",
                    (unsigned long)freeHeap, (unsigned long)ESP.getMinFreeHeap(), now);
    } else if (now - lowHeapSince >= HEAP_CRITICAL_WINDOW) {
      // Prefer not to reboot mid-shot: a WiFi-side OOM shouldn't kill a live BLE
      // session (heap exhaustion needs WiFi/HTTP churn, not BLE). While a BLE
      // client is connected, defer -- but only up to HEAP_CRITICAL_BLE_DEFER_MAX,
      // so a genuinely wedged stack still self-heals. Keep the timer armed so the
      // reboot also fires immediately once BLE disconnects.
      if (deviceConnected && now - lowHeapSince < HEAP_CRITICAL_BLE_DEFER_MAX) {
        if (now - lastDeferLog >= 5000) {  // rate-limit: the main loop has no sleep
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
    // Recovered to a healthy heap: clear the timer so any later dip needs a
    // fresh sustained window. (The old +5000 hysteresis left a 15-20 KB dead
    // band where a stale start time could trigger an instant reboot on the
    // next brief dip.)
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
  // putString returns bytes written (0 = failure). The web handler reboots
  // right after this, so a silent NVS failure would strand the scale retrying
  // stale/empty credentials -- log it loudly.
  size_t wroteSsid = preferences.putString("ssid", ssid.c_str());
  size_t wrotePass = preferences.putString("pass", pass.c_str());
  if (wroteSsid == 0 || wrotePass == 0) {
    Serial.printf("[prefs] NVS write FAILED (ssid=%u pass=%u) -- credentials may not persist\n",
                  (unsigned)wroteSsid, (unsigned)wrotePass);
  }
}

void WiFiParams::init() {
  if (!preferences.begin(wifiPrefsKey)) {
    Serial.println("[prefs] could not open NVS namespace 'wifi' -- no stored credentials");
    return;
  }
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
