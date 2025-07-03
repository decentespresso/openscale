#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "wifi_setup.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

AsyncWebServer server(80);
AsyncWebSocket websocket("/snapshot");

void startWebServer() {
  server.begin();
  Serial.println("HTTP server started");
  AsyncCallbackJsonWebHandler *wifiHandler = new AsyncCallbackJsonWebHandler(
      "/setup/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        if (jsonObj.isNull()) {
          request->send(400);
          return;
        }
        if (jsonObj["ssid"] == NULL) {
          request->send(400);
          return;
        }
        String ssid = jsonObj["ssid"];
        String pass = jsonObj["pass"];

        saveCredentials(ssid, pass);
        Serial.println("new ssid saved");
        request->send(200);
      });
  server.addHandler(wifiHandler);
  server.addHandler(&websocket);

  if (!LittleFS.begin()) {
    Serial.println("SPIFFS failed");
    return;
  }

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  Serial.println("Serving web-apps");
}
#endif
