#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "esp_system.h"
#include "wifi_setup.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

static AsyncWebServer server(80);
static AsyncWebSocket websocket("/snapshot");

void startWebServer() {
  // Handlers must be registered before server.begin(), and only once across
  // stop/start cycles — server.end() doesn't clear the handler list.
  static bool handlersRegistered = false;
  if (!handlersRegistered) {
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
          esp_restart();
        });
    server.addHandler(wifiHandler);

    // Allow multiple concurrent WebSocket clients (e.g. the on-device web UI
    // and a separate desktop/phone app at the same time). Per-loop the count
    // is bounded by cleanupClients() in src/hds.ino.
    server.addHandler(&websocket);

    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed");
    } else {
      server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
      Serial.println("Serving web-apps");
    }
    handlersRegistered = true;
  }

  server.begin();
  Serial.println("HTTP server started");
}

void stopWebServer() {
  websocket.closeAll();
  server.end();
}
#endif
