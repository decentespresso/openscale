#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

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

  server.addHandler(&websocket).addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
    // ws.count() is the current count of WS clients: this one is trying to upgrade its HTTP connection
    if (websocket.count() > 0) {
      // if we have 1 clients or more, prevent the next one to connect
      request->send(503, "text/plain", "Server is busy");
    } else {
      // process next middleware and at the end the handler
      next();
    }
  });
  server.addHandler(&websocket);

  if (!LittleFS.begin()) {
    Serial.println("SPIFFS failed");
    return;
  }

  server.on("/*.wasm", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, request->url(), "application/wasm");
  });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  Serial.println("Serving web-apps");
  server.begin();
}

void stopWebServer() {
  websocket.closeAll();
  server.end();
}
#endif