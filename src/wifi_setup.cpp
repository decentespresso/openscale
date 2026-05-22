
#include "NetworkEvents.h"
#include "WiFiType.h"
#include "config.h"  // FIRMWARE_VER for the DNS-SD TXT record
#include "esp32-hal.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

bool b_wifiEnabled = false;

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
  // Leaving WiFi modem-sleep at the Arduino default (WIFI_PS_MIN_MODEM):
  // forcing setSleep(false) caused severe packet loss and HTTP stalls when
  // BLE was active, because BT/WiFi share one 2.4GHz radio and the coex
  // layer needs WiFi's sleep windows for BT slots.
  int wifiCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (WiFi.status() == WL_CONNECT_FAILED) {
      Serial.println("Connect failed, restoring AP");
      setupAP();
      break;
    }
    wifiCounter++;
    delay(1000);
    Serial.println(".");
    // Toggle, to emulate blinking?
    b_wifiEnabled = !b_wifiEnabled;
    if (wifiCounter > 20) {
      WiFi.disconnect(true);
      delay(200);
      setupAP();
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

void setupWifi() {
  params.init();

  const char *hostname = "hds.local";
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname);

  if (params.hasCredentials()) {
    Serial.printf("trying to connect to wifi: %s\n", params.getSSID());
    connectToWifi();
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
  }

  if (!MDNS.begin("hds")) {
    Serial.println("could not set up MDNS responder");
  } else {
    // Advertise a DNS-SD service so apps can discover the scale reliably.
    // Android's NsdManager (and Bonjour/Avahi) browse for service types;
    // a bare hds.local A record alone isn't discoverable on Android.
    // Friendly instance name so a discovery UI shows "Half Decent Scale"
    // rather than the bare "hds" hostname.
    MDNS.setInstanceName("Half Decent Scale");
    MDNS.addService("decentscale", "tcp", 80);
    // FIRMWARE_VER is a (char*) cast; make it const to match the other
    // const-char* args and avoid an addServiceTxt overload ambiguity.
    MDNS.addServiceTxt("decentscale", "tcp", "fw", (const char *)FIRMWARE_VER);
    MDNS.addServiceTxt("decentscale", "tcp", "model", "hds");
    MDNS.addServiceTxt("decentscale", "tcp", "proto", "ws");
    MDNS.addServiceTxt("decentscale", "tcp", "path", "/snapshot");
    Serial.println("DNS-SD: advertised _decentscale._tcp on port 80");
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
