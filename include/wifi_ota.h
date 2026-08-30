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
static const char OTA_UPLOAD_REJECTED_ATTRIBUTE[] = "otaUploadRejected";
static const char OTA_UPLOAD_FAILED_ATTRIBUTE[] = "otaUploadFailed";

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

void handleElegantOtaStart(AsyncWebServerRequest *request) {
  if (request->hasParam("mode") &&
      request->getParam("mode")->value() == "fs") {
    request->send(400, "text/plain", "filesystem OTA requires WiFi Update");
    return;
  }

  std::unique_lock<std::mutex> otaDispatchLock(otaDispatchMutex,
                                               std::try_to_lock);
  if (!otaDispatchLock.owns_lock() || b_pullOtaRunning || b_ota) {
    request->send(409, "text/plain", "OTA already in progress");
    return;
  }
  if (request->hasParam("hash") &&
      !Update.setMD5(request->getParam("hash")->value().c_str())) {
    request->send(400, "text/plain", "MD5 parameter invalid");
    return;
  }

  onOTAStart();
  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    request->send(400, "text/plain", Update.errorString());
    return;
  }
  request->send(200, "text/plain", "OK");
}

void handleElegantOtaUpload(AsyncWebServerRequest *request,
                            const String &filename, size_t index,
                            uint8_t *data, size_t len, bool final) {
  (void)filename;
  std::unique_lock<std::mutex> otaDispatchLock(otaDispatchMutex,
                                               std::try_to_lock);
  if (!otaDispatchLock.owns_lock()) {
    request->setAttribute(OTA_UPLOAD_REJECTED_ATTRIBUTE, true);
    return;
  }
  if (request->getAttribute(OTA_UPLOAD_REJECTED_ATTRIBUTE, false)) {
    return;
  }
  if (b_pullOtaRunning || !b_ota) {
    request->setAttribute(OTA_UPLOAD_REJECTED_ATTRIBUTE, true);
    return;
  }

  bool failed = request->getAttribute(OTA_UPLOAD_FAILED_ATTRIBUTE, false);
  if (!failed && len > 0) {
    if (Update.write(data, len) != len) {
      request->setAttribute(OTA_UPLOAD_FAILED_ATTRIBUTE, true);
      failed = true;
    } else {
      onOTAProgress(index + len, request->contentLength());
    }
  }
  if (final && !Update.end(true)) {
    request->setAttribute(OTA_UPLOAD_FAILED_ATTRIBUTE, true);
  }
}

void completeElegantOtaUpload(AsyncWebServerRequest *request) {
  std::unique_lock<std::mutex> otaDispatchLock(otaDispatchMutex,
                                               std::try_to_lock);
  if (!otaDispatchLock.owns_lock() ||
      request->getAttribute(OTA_UPLOAD_REJECTED_ATTRIBUTE, false) ||
      b_pullOtaRunning || !b_ota) {
    request->send(409, "text/plain", "OTA upload rejected");
    return;
  }

  const bool success =
      !request->getAttribute(OTA_UPLOAD_FAILED_ATTRIBUTE, false) &&
      !Update.hasError();
  onOTAEnd(success);
  AsyncWebServerResponse *response = request->beginResponse(
      success ? 200 : 400, "text/plain", success ? "OK" : Update.errorString());
  response->addHeader("Connection", "close");
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void wifiOta() {
  static bool otaRegistered = false;
  if (otaRegistered) {
    return;
  }

  server.on("/ota/start", HTTP_GET, handleElegantOtaStart);
  server.on("/ota/upload", HTTP_POST, completeElegantOtaUpload,
            handleElegantOtaUpload);
  ElegantOTA.begin(&server);
  ElegantOTA.setAutoReboot(false);
  otaRegistered = true;
}
#endif
#endif
