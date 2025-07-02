
#include "WiFiType.h"
#include "esp32-hal.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>

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
  WiFi.softAP("Decent Scale");
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);

  WiFi.begin(params.getSSID(), params.getPass());
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
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
    if (wifiCounter > 10) {
      WiFi.disconnect(true);
      delay(100);
      setupAP();
      break;
    }
  }
  Serial.println("");
  Serial.println("Connected to ");
  Serial.println(WiFi.SSID().c_str());
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP().toString().c_str());
}

void setupWifi() {
  params.init();

  const char *hostname = "hds.local";
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname);

  if (params.hasCredentials()) {
    Serial.println("trying to connect to wifi");
    connectToWifi();
  } else {
    Serial.println("no wifi data found, setting up AP");
    setupAP();
  }

  if (!MDNS.begin("hds")) {
    Serial.println("could not set up MDNS responder");
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
