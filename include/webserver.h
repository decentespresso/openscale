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

// While a WS weight stream is active, HTTP file serves are token-bucket
// rate-limited: sustained traffic refills at one serve per this interval so it
// can't saturate the radio and starve the 10 Hz broadcast.
static const unsigned long HTTP_MIN_INTERVAL_WHILE_STREAMING_MS = 200;
// Bucket depth. A single web-app page load pulls HTML + CSS + a JS module graph
// (~10-12 requests in a burst); the bucket lets that through at once, then
// throttles to the refill rate above. Browsers don't retry 503 on sub-resources,
// so a too-tight limit would leave the on-device apps rendering broken.
static const int HTTP_STREAMING_BURST = 12;

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
    //     0 Hz). A token bucket (HTTP_STREAMING_BURST deep, refilling one per
    //     HTTP_MIN_INTERVAL_WHILE_STREAMING_MS) lets a normal page load burst
    //     through, then throttles sustained floods so the broadcast keeps radio
    //     priority. Time-based (no in-flight counter that could leak).
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
        static int tokens = HTTP_STREAMING_BURST;
        static unsigned long lastRefill = 0;
        unsigned long now = millis();
        unsigned long refill = (now - lastRefill) / HTTP_MIN_INTERVAL_WHILE_STREAMING_MS;
        if (refill > 0) {
          long t = tokens + (long)refill;
          tokens = t > HTTP_STREAMING_BURST ? HTTP_STREAMING_BURST : (int)t;
          lastRefill = now;
        }
        if (tokens <= 0) {
          request->send(503, "text/plain", "prioritizing data stream, retry");
          return;
        }
        tokens--;
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
