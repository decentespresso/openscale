#ifndef OTA_ROLLBACK_H
#define OTA_ROLLBACK_H

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

static bool hdsOtaPendingVerify = false;
static const uint32_t HDS_OTA_MAX_VERIFY_ATTEMPTS = 1;

extern "C" bool verifyRollbackLater() {
  return true;
}

static bool hdsOtaIsPendingVerify() {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  return running != nullptr &&
         esp_ota_get_state_partition(running, &otaState) == ESP_OK &&
         otaState == ESP_OTA_IMG_PENDING_VERIFY;
#else
  return false;
#endif
}

static bool hdsOtaResetLooksCrashy(esp_reset_reason_t resetReason) {
  return resetReason == ESP_RST_PANIC ||
         resetReason == ESP_RST_INT_WDT ||
         resetReason == ESP_RST_TASK_WDT ||
         resetReason == ESP_RST_WDT;
}

static void hdsOtaClearAttempts() {
  Preferences preferences;
  if (preferences.begin("ota_verify", false)) {
    preferences.remove("attempts");
    preferences.end();
  }
}

static uint32_t hdsOtaRecordAttempt() {
  Preferences preferences;
  if (!preferences.begin("ota_verify", false)) {
    return HDS_OTA_MAX_VERIFY_ATTEMPTS + 1;
  }
  uint32_t attempts = preferences.getUInt("attempts", 0) + 1;
  preferences.putUInt("attempts", attempts);
  preferences.end();
  return attempts;
}

static void hdsOtaRollback(const char *reason) {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  Serial.print("[ota-verify] rollback: ");
  Serial.println(reason);
  hdsOtaClearAttempts();
  Serial.flush();
  esp_ota_mark_app_invalid_rollback_and_reboot();
  delay(1000);
  ESP.restart();
#else
  Serial.print("[ota-verify] rollback requested without rollback support: ");
  Serial.println(reason);
#endif
}

void hdsOtaRollbackBegin(esp_reset_reason_t resetReason) {
  hdsOtaPendingVerify = hdsOtaIsPendingVerify();
  if (!hdsOtaPendingVerify) {
    hdsOtaClearAttempts();
    return;
  }
  uint32_t attempts = hdsOtaRecordAttempt();
  Serial.print("[ota-verify] pending attempt=");
  Serial.println(attempts);
  if (attempts > HDS_OTA_MAX_VERIFY_ATTEMPTS) {
    hdsOtaRollback("boot attempts");
  }
  if (hdsOtaResetLooksCrashy(resetReason)) {
    hdsOtaRollback("reset reason");
  }
}

static bool hdsOtaLocalChecksPass() {
  if (!LittleFS.begin()) {
    return false;
  }
  if (LittleFS.totalBytes() == 0) {
    return false;
  }
  return true;
}

void hdsOtaRollbackMarkValid() {
  if (!hdsOtaPendingVerify) {
    return;
  }
  if (!hdsOtaLocalChecksPass()) {
    hdsOtaRollback("local checks");
    return;
  }
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    hdsOtaRollback("mark valid");
    return;
  }
#endif
  hdsOtaClearAttempts();
  hdsOtaPendingVerify = false;
  Serial.println("[ota-verify] app valid");
}

#endif
