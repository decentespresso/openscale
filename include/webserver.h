#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "esp_system.h"
#include "mdns_name.h"
#include "parameter.h"
#include "wifi_setup.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <string.h>

static AsyncWebServer server(80);
static AsyncWebSocket websocket("/snapshot");

static const unsigned long HTTP_MIN_INTERVAL_WHILE_STREAMING_MS = 200;
static const int HTTP_STREAMING_BURST = 24;
static const unsigned long HTTP_PAGELOAD_BURST_RESET_MS = 1500;
static const size_t WIFI_SETUP_MAX_JSON_BYTES = 256;
static const size_t WIFI_SETUP_MAX_SSID_BYTES = 32;
static const size_t WIFI_SETUP_MAX_PASS_BYTES = 64;
static const size_t NAME_SETUP_MAX_JSON_BYTES = 128;
static const unsigned long WIFI_SETUP_RESTART_DELAY_MS = 500;
static const char *LITTLEFS_CACHE_CONTROL =
    "no-cache, must-revalidate, max-age=0";

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

static const char *parseDeviceNameRequest(JsonVariant &json) {
  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.isNull() || !jsonObj["name"].is<const char *>()) {
    return nullptr;
  }
  return jsonObj["name"].as<const char *>();
}

void startWebServer() {
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

    AsyncCallbackJsonWebHandler *nameHandler = new AsyncCallbackJsonWebHandler(
        "/setup/name", [](AsyncWebServerRequest *request, JsonVariant &json) {
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
          if (strcmp(normalized, wifiDeviceName()) == 0) {
            snprintf(body, sizeof(body),
                     "{\"name\":\"%s\",\"restarting\":false}", normalized);
            request->send(200, "application/json", body);
            return;
          }

          char stored[MDNS_NAME_BUFFER_BYTES] = {0};
          if (!saveDeviceNameForRestart(normalized, stored, sizeof(stored))) {
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

    server.addHandler(&websocket);

    if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed -- web UI unavailable");
      server.onNotFound([](AsyncWebServerRequest *request) {
        request->send(503, "text/plain",
                      "filesystem mount failed; device needs reflashing");
      });
    } else {
      server.serveStatic("/", LittleFS, "/")
          .setTryGzipFirst(true)
          .setDefaultFile("index.html")
          .setCacheControl(LITTLEFS_CACHE_CONTROL);
      Serial.println("Serving web-apps");
    }

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
