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

// While a WS weight stream is active, HTTP file serves are rate-limited to one
// per this interval so file-serving TX can't saturate the radio and starve the
// 10 Hz broadcast. ~5 serves/s still loads the web UI promptly.
static const unsigned long HTTP_MIN_INTERVAL_WHILE_STREAMING_MS = 200;

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

    // HTTP admission control, two layers, both shedding with 503 (clients
    // retry). The WS /snapshot path is always exempt -- a WS upgrade is cheap
    // and we never block or drop the live data stream.
    //
    //  1. Memory pressure: serving a static file costs ~25 KB transient heap;
    //     under churn/web-UI bursts those stack and can OOM, wedging the whole
    //     IP stack. Shed HTTP when free heap is low.
    //
    //  2. Stream priority: while a WS weight stream is active, file-serving TX
    //     can saturate the (shared, coexisting) radio and starve the 10 Hz
    //     broadcast (measured: a flood of concurrent serves drove a client to
    //     0 Hz). Rate-limit HTTP serves to one per HTTP_MIN_INTERVAL_MS so the
    //     broadcast keeps radio priority. Time-based (no in-flight counter that
    //     could leak and wedge HTTP forever).
    server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
      if (request->url() == "/snapshot") {
        next();
        return;
      }
      if (ESP.getFreeHeap() < 40000) {
        request->send(503, "text/plain", "low memory, retry");
        return;
      }
      if (websocket.count() > 0) {
        static unsigned long lastServe = 0;
        unsigned long now = millis();
        if (now - lastServe < HTTP_MIN_INTERVAL_WHILE_STREAMING_MS) {
          request->send(503, "text/plain", "prioritizing data stream, retry");
          return;
        }
        lastServe = now;
      }
      next();
    });

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
