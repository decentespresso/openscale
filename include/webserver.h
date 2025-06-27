#ifndef SCALE_WEBSERVER_H
#define SCALE_WEBSERVER_H

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

AsyncWebServer server(80);


void startWebServer() {

  server.begin();
  Serial.println("HTTP server started");
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS failed");
    return;
  }
  server.serveStatic("/apps", SPIFFS, "/");
  Serial.println("Serving web-apps");
}
#endif
