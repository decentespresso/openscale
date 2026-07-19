#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "esp_system.h"
#include "parameter.h"
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
// (up to ~15 requests in a burst today); the bucket lets that through at once,
// then throttles to the refill rate above. Browsers don't retry 503 on
// sub-resources, so a too-tight limit leaves the on-device apps broken.
static const int HTTP_STREAMING_BURST = 24;
// Refill the burst once per page navigation. This prevents a normal app load
// from inheriting an empty bucket left by an existing stream, while still
// throttling sustained floods after the page-load burst.
static const unsigned long HTTP_PAGELOAD_BURST_RESET_MS = 1500;
static const size_t WIFI_SETUP_MAX_JSON_BYTES = 256;
static const size_t WIFI_SETUP_MAX_SSID_BYTES = 32;
static const size_t WIFI_SETUP_MAX_PASS_BYTES = 64;
static const unsigned long WIFI_SETUP_RESTART_DELAY_MS = 500;
static const char *LITTLEFS_CACHE_CONTROL =
    "no-store, no-cache, must-revalidate, max-age=0";

static bool httpIsPageLoadRequest(const String &url) {
  return url == "/" || url.endsWith(".html");
}

static bool parseWifiSetupCredentials(JsonVariant &json, String &ssid, String &pass) {
  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.isNull()) {
    return false;
  }
  if (!jsonObj["ssid"].is<const char *>()) {
    return false;
  }
  if (jsonObj["pass"] != NULL && !jsonObj["pass"].is<const char *>()) {
    return false;
  }

  ssid = jsonObj["ssid"].as<String>();
  pass = jsonObj["pass"].is<const char *>() ? jsonObj["pass"].as<String>() : "";
  return ssid.length() <= WIFI_SETUP_MAX_SSID_BYTES &&
         pass.length() <= WIFI_SETUP_MAX_PASS_BYTES;
}

void startWebServer() {
  // Handlers must be registered before server.begin(), and only once across
  // stop/start cycles — server.end() doesn't clear the handler list.
  static bool handlersRegistered = false;
  if (!handlersRegistered) {
    AsyncCallbackJsonWebHandler *wifiHandler = new AsyncCallbackJsonWebHandler(
        "/setup/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
          String ssid;
          String pass;
          if (!parseWifiSetupCredentials(json, ssid, pass)) {
            request->send(400);
            return;
          }

          if (!saveCredentialsForRestart(ssid, pass)) {
            request->send(500, "application/json",
                          "{\"error\":\"wifi_credentials_save_failed\"}");
            return;
          }
          Serial.println("new ssid saved");
          request->send(200);
          remoteQueueResetAt(millis() + WIFI_SETUP_RESTART_DELAY_MS);
        });
    wifiHandler->setMaxContentLength(WIFI_SETUP_MAX_JSON_BYTES);
    server.addHandler(wifiHandler);

    // Allow multiple concurrent WebSocket clients (e.g. the on-device web UI
    // and a separate desktop/phone app at the same time). Per-loop the count
    // is bounded by cleanupClients() in src/hds.ino.
    server.addHandler(&websocket);

    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed -- web UI unavailable");
      // Without serveStatic, requests fall through to a bare 404. Serve an
      // explanatory 503 instead so the failure is diagnosable, not confusing.
      server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(503, "text/plain",
                      "filesystem mount failed; device needs reflashing");
      });
    } else {
      // LittleFS apps are updated in-place; avoid stale inline JS/CSS and modules
      // after a firmware/filesystem update.
      server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html")
          .setCacheControl(LITTLEFS_CACHE_CONTROL);
      Serial.println("Serving web-apps");
    }

    // HTTP admission control, two layers, both shedding with 503 (clients
    // retry). Any request to the /snapshot data endpoint is exempt -- matched
    // by URL, not just the WS upgrade -- so we never block or drop the live
    // data stream.
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
    //     priority. Time-based (no in-flight counter that could leak). The
    //     bucket is clamped at HTTP_STREAMING_BURST, so accumulated idle time
    //     never pre-fills it beyond the burst depth.
    server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
      const String &url = request->url();
      if (url == "/snapshot") {
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
        static unsigned long lastPageLoadBoost = 0;
        unsigned long now = millis();
        if (httpIsPageLoadRequest(url) &&
            (lastPageLoadBoost == 0 ||
             now - lastPageLoadBoost >= HTTP_PAGELOAD_BURST_RESET_MS)) {
          tokens = HTTP_STREAMING_BURST;
          lastPageLoadBoost = now;
          lastRefill = now;
        }
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
