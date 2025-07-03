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
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WiFi.h>

extern AsyncWebServer server;
const char *ssid = "DecentScale";
const char *password = "12345678";
unsigned long ota_progress_millis = 0;
unsigned long t_otaEnd = 0;

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  b_ota = true;
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 50) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current,
                  final);
    Serial.printf("Progress: %u%%\n", (current * 100) / final);
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "Uploading: %u%%",
             (current * 100) / final);
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
        u8g2.drawUTF8(AC((char *)"OTA update finished"), AM(),
                      (char *)"OTA update finished");
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
        u8g2.drawUTF8(AC((char *)"OTA update failed"), AM(),
                      (char *)"OTA update failed");
      } while (u8g2.nextPage());
    }
  }
}

void wifiOta() {
  ElegantOTA.begin(&server); // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.setAutoReboot(true);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
}
#endif // WIFI_OTA_H
#endif
