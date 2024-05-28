#ifdef WIFI
#ifndef WIFI_OTA_H
#define WIFI_OTA_H
#include "so_display.h"
#include <AsyncElegantOTA.h>
#if !(defined(ESP32))
#error This code is intended to run on the ESP32 platform! Please check your Tools->Board setting.
#endif
#include <ESPAsync_WiFiManager.h>
AsyncWebServer webServer(80);

AsyncDNSServer dnsServer;

void wifiOTA() {
  // if (!SerialBT.connected()) {
  //   SerialBT.begin("soso D.R.S");  //Re-start Bluetooth if not connected
  // }
  refreshOLED((char *)"WiFi升级", (char *)"蓝牙已开启", FONT_M);
  delay(1000);
  Serial.print("\nStarting Async_AutoConnect_ESP32_minimal on " + String(ARDUINO_BOARD));
  Serial.println(ESP_ASYNC_WIFIMANAGER_VERSION);
  refreshOLED((char *)"请连接这个WiFi", (char *)"SSID: D.R.S", FONT_M);
  delay(1000);
  ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, "Async_AutoConnect");
  if (digitalRead(BUTTON_SET) == LOW)
    ESPAsync_wifiManager.resetSettings();  //reset saved settings
  //ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 132, 1), IPAddress(192, 168, 132, 1), IPAddress(255, 255, 255, 0));
  if (!ESPAsync_wifiManager.autoConnect("D.R.S")) {
    /*
    in ESPAsync_WiFiManager\src\ESPAsync_WiFiManager-Impl.h
    
    at line 470 should be like this:
bool ESPAsync_WiFiManager::autoConnect(char const *apName,
                                       char const *apPassword) {
#if AUTOCONNECT_NO_INVALIDATE
  LOGINFO(F("\nAutoConnect using previously saved SSID/PW, but keep previous "
            "settings"));
  // Connect to previously saved SSID/PW, but keep previous settings
  connectWifi();
#else
  LOGINFO(F("\nAutoConnect using previously saved SSID/PW, but invalidate "
            "previous settings"));
  // Connect to previously saved SSID/PW, but invalidate previous settings
  connectWifi(WiFi_SSID(), WiFi_Pass());
#endif

  unsigned long startedAt = millis();

  while (millis() - startedAt < 10000) {
    // delay(100);
    delay(200);
#ifdef ESP32
    if (digitalRead(BUTTON_SET) == LOW)
      return false;
#endif
    if (WiFi.status() == WL_CONNECTED) {
      float waited = (millis() - startedAt);
      LOGWARN1(F("Connected after waiting (s) :"), waited / 1000);
      LOGWARN1(F("Local ip ="), WiFi.localIP());

      return true;
    }
  }

  return startConfigPortal(apName, apPassword);
}
    */
    refreshOLED((char *)"退出WiFi连接", FONT_M);
    delay(1000);
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    char *myCharArray = new char[WiFi.localIP().toString().length() + 1];
    strcpy(myCharArray, WiFi.localIP().toString().c_str());
    refreshOLED((char *)"请访问以下地址", myCharArray, FONT_M);
    delay(3000);
    Serial.print(F("Connected. Local IP: "));
    Serial.println(WiFi.localIP());

  } else {
    if (digitalRead(BUTTON_TARE) == HIGH)
      Serial.println(ESPAsync_wifiManager.getStatus(WiFi.status()));
    else
      return;
  }
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    //request->send(200, "text/plain", "Hi! I am MyESP32S.");
    String ipAddress = "http://" + WiFi.localIP().toString() + "/update";
    request->redirect(ipAddress);
  });

  AsyncElegantOTA.begin(&webServer);  // Start ElegantOTA WITHOUT username & password
  //AsyncElegantOTA.begin(&webServer, FOTA_USERNAME, FOTA_PASSWORD); // Start ElegantOTA with username & password
  webServer.begin();
  Serial.println("HTTP server started");
}
#endif  //WIFI_OTA_H
#endif