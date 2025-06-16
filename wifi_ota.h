#ifdef WIFIOTA
#ifndef WIFI_OTA_H
#define WIFI_OTA_H
#include "display.h"
/* please remember to edit the ESPAsyncWebServer.h
add the following line 

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

or edit the 

#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
  #define ELEGANTOTA_USE_ASYNC_WEBSERVER 0
#endif

into

#ifndef ELEGANTOTA_USE_ASYNC_WEBSERVER
  #define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
#endif
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1
*/
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

const char *ssid = "DecentScale";
const char *password = "12345678";
AsyncWebServer server(80);
unsigned long ota_progress_millis = 0;
unsigned long t_otaEnd = 0;

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 50) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
    Serial.printf("Progress: %u%%\n", (current * 100) / final);
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Uploading: %u%%", (current * 100) / final);
    u8g2.firstPage();
    u8g2.setFont(FONT_S);
    if (b_screenFlipped)
      u8g2.setDisplayRotation(U8G2_R0);
    else
      u8g2.setDisplayRotation(U8G2_R2);
    do {
      u8g2.drawUTF8(AC((char *)trim(buffer)), AM(), (char *)trim(buffer));
    } while (u8g2.nextPage());
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  t_otaEnd = millis();
  if (success) {
    Serial.println("OTA update finished successfully!");
    u8g2.setFont(FONT_S);
    if (b_screenFlipped)
      u8g2.setDisplayRotation(U8G2_R0);
    else
      u8g2.setDisplayRotation(U8G2_R2);
    while (millis() - t_otaEnd < 1000) {
      u8g2.firstPage();
      do {
        u8g2.drawUTF8(AC((char *)"OTA update finished"), AM(), (char *)"OTA update finished");
      } while (u8g2.nextPage());
    }
  } else {
    Serial.println("There was an error during OTA update!");
    u8g2.setFont(FONT_S);
    if (b_screenFlipped)
      u8g2.setDisplayRotation(U8G2_R0);
    else
      u8g2.setDisplayRotation(U8G2_R2);
    while (millis() - t_otaEnd < 1000) {
      u8g2.firstPage();
      do {
        u8g2.drawUTF8(AC((char *)"OTA update failed"), AM(), (char *)"OTA update failed");
      } while (u8g2.nextPage());
    }
  }
}

void wifiOta() {
  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);
  do {
    u8g2.drawUTF8(AC((char *)"Starting OTA"), AM(), (char *)"Starting OTA");
  } while (u8g2.nextPage());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);
  Serial.println("");

  Serial.print("WiFi Access Point: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Redirect to "/update"
    request->redirect("/update");
  });

  ElegantOTA.begin(&server);  // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.setAutoReboot(true);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
  Serial.println("HTTP server started");
  b_ota = true;
  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);
  do {
    char ver[50];
    sprintf(ver, "%s %s", PCB_VER, FIRMWARE_VER);
    //u8g2.drawUTF8(AC((char *)"Please connect WiFi"), u8g2.getMaxCharHeight() + i_margin_top - 5, (char *)"Please connect WiFi");

    u8g2.drawUTF8(AC((char *)"WiFi: DecentScale"), u8g2.getMaxCharHeight() + i_margin_top - 5, (char *)"WiFi: DecentScale");
    u8g2.drawUTF8(AC((char *)"Pwd: 12345678"), AM(), (char *)"Pwd: 12345678");
    u8g2.drawUTF8(AC(trim(ver)), LCDHeight - i_margin_bottom + 5, trim(ver));
  } while (u8g2.nextPage());
}
#endif  //WIFI_OTA_H
#endif