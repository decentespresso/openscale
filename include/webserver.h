#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "config.h"
#if HDS_FEATURE_WEBSERVER
#include "esp_system.h"
#include "mdns_name.h"
#include "parameter.h"
#include "filesystem_recovery.h"
#include "wifi_setup.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#if HDS_FEATURE_LITTLEFS
#include <LittleFS.h>
#endif
#include <WiFi.h>
#include <string.h>
#include "wifi_setup_page.h"
#include "wifi_setup_routes.h"

static AsyncWebServer server(80);
#if HDS_FEATURE_WEBSOCKET
static AsyncWebSocket websocket("/snapshot");
#endif

static const unsigned long HTTP_MIN_INTERVAL_WHILE_STREAMING_MS = 200;
static const int HTTP_STREAMING_BURST = 24;
static const unsigned long HTTP_PAGELOAD_BURST_RESET_MS = 1500;
static const size_t NAME_SETUP_MAX_JSON_BYTES = 128;
static const unsigned long WIFI_SETUP_RESTART_DELAY_MS = 500;
static const char *LITTLEFS_CACHE_CONTROL =
    "no-cache, must-revalidate, max-age=0";

static bool httpIsPageLoadRequest(const String &url) {
  return url == "/" || url.endsWith(".html");
}

static const char *parseDeviceNameRequest(JsonVariant &json) {
  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.isNull() || !jsonObj["name"].is<const char *>()) {
    return nullptr;
  }
  return jsonObj["name"].as<const char *>();
}

void startWebServer() {
#if HDS_FEATURE_LITTLEFS
  webFilesystemReady.store(!filesystemRecoveryActive.load() && LittleFS.begin());
  if (!webFilesystemReady.load()) {
    filesystemRecoveryActive.store(true);
    Serial.println("LittleFS unavailable -- serving WiFi setup");
  }
#endif
  static bool handlersRegistered = false;
  if (!handlersRegistered) {
    registerWifiSetupRoutes(server);
    server.on("/setup/wifi.js", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "application/javascript", HDS_WIFI_SETUP_SCRIPT);
    });
    server.on("/setup/wifi.css", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/css", HDS_WIFI_SETUP_STYLE);
    });

    AsyncCallbackJsonWebHandler *nameHandler = new AsyncCallbackJsonWebHandler(
        "/setup/name", [](AsyncWebServerRequest *request, JsonVariant &json) {
          if (!wifiRequestOriginAllowed(request)) {
            request->send(403, "application/json", "{\"error\":\"origin_denied\"}");
            return;
          }
          if (!wifiRequestDeviceAllowed(request)) return;
          const char *requested = parseDeviceNameRequest(json);
          if (requested == nullptr) {
            request->send(400, "application/json",
                          "{\"error\":\"device_name_invalid\"}");
            return;
          }

          char normalized[MDNS_NAME_BUFFER_BYTES] = {0};
          if (!mdnsNameNormalize(requested, normalized, sizeof(normalized))) {
            request->send(400, "application/json",
                          "{\"error\":\"device_name_invalid\"}");
            return;
          }

          char body[MDNS_NAME_BUFFER_BYTES + 40];
          if (!wifiReserveExternalOperation()) {
            request->send(409, "application/json", "{\"error\":\"wifi_busy\"}");
            return;
          }
          if (strcmp(normalized, wifiDeviceName()) == 0) {
            wifiReleaseExternalOperation();
            snprintf(body, sizeof(body),
                     "{\"name\":\"%s\",\"restarting\":false}", normalized);
            request->send(200, "application/json", body);
            return;
          }

          char stored[MDNS_NAME_BUFFER_BYTES] = {0};
          if (!saveDeviceNameForRestart(normalized, stored, sizeof(stored))) {
            wifiReleaseExternalOperation();
            request->send(500, "application/json",
                          "{\"error\":\"device_name_save_failed\"}");
            return;
          }

          Serial.printf("device name saved: %s\n", stored);
          snprintf(body, sizeof(body), "{\"name\":\"%s\",\"restarting\":true}", stored);
          request->send(200, "application/json", body);
          remoteQueueResetAt(millis() + WIFI_SETUP_RESTART_DELAY_MS);
        });
    nameHandler->setMaxContentLength(NAME_SETUP_MAX_JSON_BYTES);
    server.addHandler(nameHandler);

#if HDS_FEATURE_WEBSOCKET
    server.addHandler(&websocket);
#endif

#if HDS_FEATURE_LITTLEFS
    server.serveStatic("/", LittleFS, "/")
        .setTryGzipFirst(true)
        .setDefaultFile("index.html")
        .setCacheControl(LITTLEFS_CACHE_CONTROL)
        .setFilter([](AsyncWebServerRequest *) {
          return webFilesystemReady.load() && !filesystemRecoveryActive.load();
        });
#endif
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncWebServerResponse *response = request->beginResponse(200, "text/html", HDS_WIFI_SETUP_PAGE);
      response->addHeader("Cache-Control", "no-store");
      request->send(response);
    });

    server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
      const String &url = request->url();
      const bool activeOtaUpload = b_ota && !b_pullOtaRunning && url == "/ota/upload";
      if (!activeOtaUpload && (wifiSetupBusy() || b_ota) &&
          (url == "/setup/name" || url == "/setup/wifi" ||
           (url == "/setup/wifi/scan" && request->method() == HTTP_POST) ||
           url == "/update" || url.startsWith("/ota/"))) {
        request->send(409, "application/json", "{\"error\":\"wifi_busy\"}");
        return;
      }
#if HDS_FEATURE_PULL_OTA
      if (b_pullOtaRunning && (url == "/update" || url.startsWith("/ota/"))) {
        request->send(409, "text/plain", "pull OTA in progress");
        return;
      }
#endif
#if HDS_FEATURE_ELEGANT_OTA
      if (url == "/ota/start" && request->hasParam("mode") &&
          request->getParam("mode")->value() == "fs") {
        request->send(400, "text/plain", "filesystem OTA requires WiFi Update");
        return;
      }
      if (b_ota && !b_pullOtaRunning && url == "/ota/upload") {
        next();
        return;
      }
#endif
      if (b_ota) {
        request->send(503, "text/plain", "OTA in progress");
        return;
      }
      if (url == "/snapshot") {
        next();
        return;
      }
      if (ESP.getFreeHeap() < 40000) {
        request->send(503, "text/plain", "low memory, retry");
        return;
      }
#if HDS_FEATURE_ELEGANT_OTA
      if (url == "/ota/start") {
        if (!wifiRequestOriginAllowed(request)) {
          request->send(403, "application/json", "{\"error\":\"origin_denied\"}");
          return;
        }
        if (!wifiReserveExternalOperation()) {
          request->send(409, "application/json", "{\"error\":\"wifi_busy\"}");
          return;
        }
        next();
        if (!b_ota) wifiReleaseExternalOperation();
        return;
      }
      if (url.startsWith("/ota/")) {
        next();
        return;
      }
#endif
#if HDS_FEATURE_WEBSOCKET
      if (websocketHasClients()) {
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
#endif
      next();
    });

    handlersRegistered = true;
  }

  server.begin();
  Serial.println("HTTP server started");
}

void stopWebServer() {
#if HDS_FEATURE_WEBSOCKET
  websocket.closeAll();
#endif
  server.end();
}
#endif
#endif
