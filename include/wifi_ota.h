#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include "config.h"

#if HDS_FEATURE_ELEGANT_OTA
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
// Give the success message time to draw and the HTTP response time to flush
// before the reset lands. Matches the delay ElegantOTA's own auto-reboot used
// before we routed the restart through reset() instead.
static const unsigned long OTA_RESTART_DELAY_MS = 2000;

uint8_t calculateOtaPercent(size_t current, size_t final) {
  if (final == 0) {
    return 0;
  }
  if (current >= final) {
    return 100;
  }
  size_t rawPercent = (current * 100U) / final;
  return rawPercent > 100 ? 100 : (uint8_t)rawPercent;
}

void queueOtaDisplay(uint8_t state, uint8_t percent = 0) {
  portENTER_CRITICAL(&otaDisplayMux);
  otaDisplayState = state;
  otaDisplayPercent = percent;
  portEXIT_CRITICAL(&otaDisplayMux);
}

void processOtaDisplayUpdate() {
  portENTER_CRITICAL(&otaDisplayMux);
  uint8_t state = otaDisplayState;
  uint8_t percent = otaDisplayPercent;
  otaDisplayState = OTA_DISPLAY_NONE;
  portEXIT_CRITICAL(&otaDisplayMux);

  if (state == OTA_DISPLAY_NONE) {
    return;
  }

  char buffer[50];
  if (state == OTA_DISPLAY_PROGRESS) {
    snprintf(buffer, sizeof(buffer), "Uploading: %u%%", percent);
  } else if (state == OTA_DISPLAY_SUCCESS) {
    snprintf(buffer, sizeof(buffer), "OTA update finished");
  } else {
    snprintf(buffer, sizeof(buffer), "OTA update failed");
    b_ota = false;
  }

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
    uint8_t percent = calculateOtaPercent(current, final);
    Serial.printf("Progress: %u%%\n", percent);
    queueOtaDisplay(OTA_DISPLAY_PROGRESS, percent);
  }
}

void onOTAEnd(bool success) {
  // Runs on the AsyncTCP task: queue only, never touch mDNS/WiFi/hardware
  // from here. Log when OTA has finished.
  t_otaEnd = millis();
  if (success) {
    Serial.println("OTA update finished successfully!");
    queueOtaDisplay(OTA_DISPLAY_SUCCESS);
    // Auto-reboot is disabled below so the restart goes through reset()'s
    // mDNS withdrawal on the main loop instead of ElegantOTA's bare
    // ESP.restart().
    remoteQueueResetAt(millis() + OTA_RESTART_DELAY_MS);
  } else {
    Serial.println("There was an error during OTA update!");
    queueOtaDisplay(OTA_DISPLAY_FAILURE);
  }
}

void wifiOta() {
  static bool otaRegistered = false;
  if (otaRegistered) {
    return;
  }

  ElegantOTA.begin(&server); // Start ElegantOTA
  // ElegantOTA callbacks
  // Auto-reboot off: onOTAEnd() queues the restart through reset() instead,
  // so a browser-triggered update withdraws mDNS like every other reboot path.
  ElegantOTA.setAutoReboot(false);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  otaRegistered = true;
}
#endif

#endif
