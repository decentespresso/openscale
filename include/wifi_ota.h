#ifndef WIFI_OTA_H
#define WIFI_OTA_H
#include "config.h"
#if HDS_FEATURE_ELEGANT_OTA
#include "display.h"
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <WiFi.h>

extern AsyncWebServer server;
const char *ssid = "DecentScale";
const char *password = "12345678";
unsigned long ota_progress_millis = 0;
unsigned long t_otaEnd = 0;
static const unsigned long OTA_RESTART_DELAY_MS = 2000;
static const unsigned long OTA_ACTIVITY_TIMEOUT_MS = 30000;
static const unsigned long OTA_PROGRESS_INTERVAL_MS = 500;

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
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
}

void recordElegantOtaActivity(unsigned long activityAt) {
  portENTER_CRITICAL(&otaDisplayMux);
  otaActivityAt = activityAt;
  portEXIT_CRITICAL(&otaDisplayMux);
}

void processElegantOtaTimeout() {
  if (!b_ota || b_pullOtaRunning) {
    return;
  }

  const unsigned long now = millis();
  portENTER_CRITICAL(&otaDisplayMux);
  const unsigned long activityAt = otaActivityAt;
  portEXIT_CRITICAL(&otaDisplayMux);
  if (now - activityAt < OTA_ACTIVITY_TIMEOUT_MS) {
    return;
  }

  Serial.println("OTA update timed out; restarting");
  remoteQueueOtaResetAt(now);
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

#if HDS_ENABLE_ENERGY_MENU
  invalidateEnergyOledFrame();
#endif
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
#if HDS_ENABLE_ENERGY_MENU
  recordEnergyActivity();
#endif
  Serial.println("OTA update started!");
  std::lock_guard<std::mutex> otaDispatchLock(otaDispatchMutex);
  recordElegantOtaActivity(millis());
  portENTER_CRITICAL(&wsPendingMux);
  b_ota = true;
  portEXIT_CRITICAL(&wsPendingMux);
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
}

void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis >= OTA_PROGRESS_INTERVAL_MS) {
    ota_progress_millis = millis();
    recordElegantOtaActivity(ota_progress_millis);
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current,
                  final);
    uint8_t percent = calculateOtaPercent(current, final);
    Serial.printf("Progress: %u%%\n", percent);
    queueOtaDisplay(OTA_DISPLAY_PROGRESS, percent);
  }
}

void onOTAEnd(bool success) {
  t_otaEnd = millis();
  if (success) {
    Serial.println("OTA update finished successfully!");
    queueOtaDisplay(OTA_DISPLAY_SUCCESS);
    remoteQueueOtaResetAt(millis() + OTA_RESTART_DELAY_MS);
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

  ElegantOTA.begin(&server);
  ElegantOTA.setAutoReboot(false);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
  otaRegistered = true;
}
#endif
#endif
