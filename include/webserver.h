#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include "config.h"

#if HDS_FEATURE_WEBSERVER

#include "esp_system.h"
#include "parameter.h"
#include "wifi_setup.h"
#if HDS_FEATURE_MDNS
#include "mdns_name.h"
#endif
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#if HDS_FEATURE_LITTLEFS
#include <LittleFS.h>
#endif
#include <WiFi.h>
#include <string.h>

static AsyncWebServer server(80);
#if HDS_FEATURE_WEBSOCKET
static AsyncWebSocket websocket("/snapshot");
#endif

static const size_t WIFI_SETUP_MAX_JSON_BYTES = 256;
static const size_t WIFI_SETUP_MAX_SSID_BYTES = 32;
static const size_t WIFI_SETUP_MAX_PASS_BYTES = 64;
static const unsigned long WIFI_SETUP_RESTART_DELAY_MS = 500;
#if HDS_FEATURE_MDNS
static const size_t NAME_SETUP_MAX_JSON_BYTES = 128;
#endif
#if HDS_FEATURE_LITTLEFS
static const char *LITTLEFS_CACHE_CONTROL =
    "no-cache, must-revalidate, max-age=0";
#endif
#if HDS_FEATURE_WEBSOCKET
static const unsigned long HTTP_MIN_INTERVAL_WHILE_STREAMING_MS = 200;
static const int HTTP_STREAMING_BURST = 24;
static const unsigned long HTTP_PAGELOAD_BURST_RESET_MS = 1500;
#endif

#if !HDS_FEATURE_LITTLEFS
#if HDS_FEATURE_MDNS
static const char HDS_SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenScale setup</title>
<main><h1>OpenScale setup</h1><form id="wifi"><label>SSID <input id="ssid" maxlength="32" required></label><label>Password <input id="pass" type="password" maxlength="64"></label><button>Save WiFi</button></form><form id="name"><label>Device name <input id="device" maxlength="31" required></label><button>Save name</button></form><output id="status"></output></main>
<style>body{font:16px system-ui;margin:2rem;max-width:32rem}form{display:grid;gap:.75rem;margin:1.5rem 0}label{display:grid;gap:.25rem}input,button{font:inherit;padding:.6rem}output{white-space:pre-wrap}</style>
<script>const statusEl=document.querySelector('#status'),ssidEl=document.querySelector('#ssid'),passEl=document.querySelector('#pass'),deviceEl=document.querySelector('#device');async function post(path,body){statusEl.textContent='Saving...';const response=await fetch(path,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(body)});statusEl.textContent=response.ok?'Saved. The scale may restart.':`Error ${response.status}`;}document.querySelector('#wifi').onsubmit=e=>{e.preventDefault();post('/setup/wifi',{ssid:ssidEl.value,pass:passEl.value});};document.querySelector('#name').onsubmit=e=>{e.preventDefault();post('/setup/name',{name:deviceEl.value});};</script>)HTML";
#else
static const char HDS_SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenScale setup</title>
<main><h1>OpenScale setup</h1><form id="wifi"><label>SSID <input id="ssid" maxlength="32" required></label><label>Password <input id="pass" type="password" maxlength="64"></label><button>Save WiFi</button></form><output id="status"></output></main>
<style>body{font:16px system-ui;margin:2rem;max-width:32rem}form{display:grid;gap:.75rem;margin:1.5rem 0}label{display:grid;gap:.25rem}input,button{font:inherit;padding:.6rem}output{white-space:pre-wrap}</style>
<script>const statusEl=document.querySelector('#status'),ssidEl=document.querySelector('#ssid'),passEl=document.querySelector('#pass');document.querySelector('#wifi').onsubmit=async e=>{e.preventDefault();statusEl.textContent='Saving...';const response=await fetch('/setup/wifi',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({ssid:ssidEl.value,pass:passEl.value})});statusEl.textContent=response.ok?'Saved. The scale may restart.':`Error ${response.status}`;};</script>)HTML";
#endif
#endif

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

#if HDS_FEATURE_MDNS
static const char *parseDeviceNameRequest(JsonVariant &json) {
  JsonObject jsonObj = json.as<JsonObject>();
  if (jsonObj.isNull() || !jsonObj["name"].is<const char *>()) {
    return nullptr;
  }
  return jsonObj["name"].as<const char *>();
}
#endif

static void registerHttpSetupHandlers() {
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
#if HDS_FEATURE_MDNS
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
        snprintf(body, sizeof(body),
                 "{\"name\":\"%s\",\"restarting\":true}", stored);
        request->send(200, "application/json", body);
        remoteQueueResetAt(millis() + WIFI_SETUP_RESTART_DELAY_MS);
      });
  nameHandler->setMaxContentLength(NAME_SETUP_MAX_JSON_BYTES);
  server.addHandler(nameHandler);
#endif
}

static void registerWebsocketHandler() {
#if HDS_FEATURE_WEBSOCKET
  server.addHandler(&websocket);
#endif
}

static void registerStaticServing() {
#if HDS_FEATURE_LITTLEFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed -- web UI unavailable");
    server.onNotFound([](AsyncWebServerRequest *request) {
      request->send(503, "text/plain",
                    "filesystem mount failed; device needs reflashing");
    });
    return;
  }
  server.serveStatic("/", LittleFS, "/")
      .setTryGzipFirst(true)
      .setDefaultFile("index.html")
      .setCacheControl(LITTLEFS_CACHE_CONTROL);
  Serial.println("Serving web-apps");
#else
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html; charset=utf-8", HDS_SETUP_PAGE);
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "not found");
  });
  Serial.println("Serving inline setup page");
#endif
}

static void registerHttpAdmissionControl() {
  server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
#if HDS_FEATURE_WEBSOCKET
    const String &url = request->url();
    if (url == "/snapshot") {
      next();
      return;
    }
#endif
    if (ESP.getFreeHeap() < 40000) {
      request->send(503, "text/plain", "low memory, retry");
      return;
    }
#if HDS_FEATURE_WEBSOCKET
    if (websocket.count() > 0) {
      static int tokens = HTTP_STREAMING_BURST;
      static unsigned long lastRefill = 0;
      static unsigned long lastPageLoadBoost = 0;
      unsigned long now = millis();
      bool pageLoad = url == "/" || url.endsWith(".html");
      if (pageLoad &&
          (lastPageLoadBoost == 0 ||
           now - lastPageLoadBoost >= HTTP_PAGELOAD_BURST_RESET_MS)) {
        tokens = HTTP_STREAMING_BURST;
        lastPageLoadBoost = now;
        lastRefill = now;
      }
      unsigned long refill =
          (now - lastRefill) / HTTP_MIN_INTERVAL_WHILE_STREAMING_MS;
      if (refill > 0) {
        long refilled = tokens + (long)refill;
        tokens = refilled > HTTP_STREAMING_BURST
                     ? HTTP_STREAMING_BURST
                     : (int)refilled;
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
}

void startWebServer() {
  static bool handlersRegistered = false;
  if (!handlersRegistered) {
    registerHttpSetupHandlers();
    registerWebsocketHandler();
    registerStaticServing();
    registerHttpAdmissionControl();
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
