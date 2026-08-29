#ifndef WEBSOCKET_H
#define WEBSOCKET_H
#include "config.h"
#include "declare.h"
#include "parameter.h"
#include "power.h"
#include "display.h"
#if HDS_FEATURE_WEBSOCKET
#include "wifi_setup.h"
#include "webserver.h"
#include <cstdlib>
#include <cstring>
#include <string_view>
#endif

#if HDS_FEATURE_PULL_OTA
void wifiUpdate();
void wifiUpdate(const PullOtaTargetVersion &target);
#endif
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif

#if HDS_FEATURE_WEBSOCKET
std::string_view websocketTrimView(std::string_view value) {
  while (!value.empty() && static_cast<unsigned char>(value.front()) <= ' ') {
    value.remove_prefix(1);
  }
  while (!value.empty() && static_cast<unsigned char>(value.back()) <= ' ') {
    value.remove_suffix(1);
  }
  return value;
}

char websocketAsciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

bool websocketEqualsIgnoreCase(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); i++) {
    if (websocketAsciiLower(left[i]) != websocketAsciiLower(right[i])) {
      return false;
    }
  }
  return true;
}

bool websocketStartsWithIgnoreCase(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         websocketEqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

float websocketFloatValue(std::string_view value) {
  value = websocketTrimView(value);
  char text[32];
  if (value.empty() || value.size() >= sizeof(text)) {
    return 0;
  }
  memcpy(text, value.data(), value.size());
  text[value.size()] = '\0';
  return strtof(text, nullptr);
}

unsigned long websocketUnsignedValue(std::string_view value) {
  value = websocketTrimView(value);
  char text[32];
  if (value.empty() || value.size() >= sizeof(text)) {
    return 0;
  }
  memcpy(text, value.data(), value.size());
  text[value.size()] = '\0';
  return strtoul(text, nullptr, 10);
}

unsigned long websocketIntervalForRate(float hz) {
  if (fabs(hz - 2.0) < 0.01) {
    return WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS;
  }
  if (fabs(hz - 5.0) < 0.01) {
    return WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS;
  }
  if (fabs(hz - 10.0) < 0.01) {
    return WEBSOCKET_10HZ_NOTIFY_INTERVAL_MS;
  }
  return 0;
}

unsigned long websocketIntervalForLabel(std::string_view label) {
  label = websocketTrimView(label);
  if (websocketEqualsIgnoreCase(label, "2") ||
      websocketEqualsIgnoreCase(label, "2hz") ||
      websocketEqualsIgnoreCase(label, "2k")) {
    return WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS;
  }
  if (websocketEqualsIgnoreCase(label, "5") ||
      websocketEqualsIgnoreCase(label, "5hz") ||
      websocketEqualsIgnoreCase(label, "5k")) {
    return WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS;
  }
  if (websocketEqualsIgnoreCase(label, "10") ||
      websocketEqualsIgnoreCase(label, "10hz") ||
      websocketEqualsIgnoreCase(label, "10k")) {
    return WEBSOCKET_10HZ_NOTIFY_INTERVAL_MS;
  }
  return 0;
}

unsigned long websocketRateForInterval(unsigned long intervalMs) {
  if (intervalMs == WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS) {
    return 2;
  }
  if (intervalMs == WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS) {
    return 5;
  }
  if (intervalMs == WEBSOCKET_10HZ_NOTIFY_INTERVAL_MS) {
    return 10;
  }
  return 0;
}

const char *websocketButtonName(int buttonNumber) {
  if (buttonNumber == 1) {
    return "circle";
  }
  if (buttonNumber == 2) {
    return "square";
  }
  return "unknown";
}

const char *websocketPressName(int buttonShortPress) {
  if (buttonShortPress == 1) {
    return "short";
  }
  if (buttonShortPress == 2) {
    return "long";
  }
  return "release";
}

const char *websocketPowerOffReason(int reason) {
  switch (reason) {
    case 0: return "disabled";
    case 1: return "circle_double_click";
    case 2: return "square_double_click";
    case 3: return "low_battery";
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 4: return "gyro";
#endif
    default: return "unknown";
  }
}

bool websocketIsCharging() {
#if defined(USB_DET)
  return digitalRead(USB_DET) == LOW;
#else
  return digitalRead(BATTERY_CHARGING) == LOW;
#endif
}

int websocketBatteryPercent() {
  float perc = (f_batteryVoltage - showEmptyBatteryBelowVoltage) /
               (showFullBatteryAboveVoltage - showEmptyBatteryBelowVoltage) * 100.0f;
  if (perc < 0) perc = 0;
  if (perc > 100) perc = 100;
  return (int)perc;
}
#endif

inline void remoteQueuePending(uint32_t bits) {
  portENTER_CRITICAL(&wsPendingMux);
  if (bits & WSP_RESET) {
    pendingResetAt = 0;
  }
  wsPendingMask |= bits;
  portEXIT_CRITICAL(&wsPendingMux);
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
}

inline void remoteReplacePending(uint32_t setBits, uint32_t clearBits) {
  portENTER_CRITICAL(&wsPendingMux);
  wsPendingMask = (wsPendingMask & ~clearBits) | setBits;
  portEXIT_CRITICAL(&wsPendingMux);
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
}

inline void remoteQueueSamplesInUse(uint8_t samplesInUse) {
  portENTER_CRITICAL(&wsPendingMux);
  pendingSamplesInUse = samplesInUse;
  wsPendingMask |= WSP_SET_SAMPLES;
  portEXIT_CRITICAL(&wsPendingMux);
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
}

inline bool remoteQueueWifiUpdate(const PullOtaTargetVersion &target) {
  bool queued = false;
  portENTER_CRITICAL(&wsPendingMux);
  if (!(wsPendingMask & WSP_WIFI_UPDATE) && !pendingOtaDispatching &&
      !b_ota && !b_pullOtaRunning) {
    pendingOtaTargetMajor = target.major;
    pendingOtaTargetMinor = target.minor;
    pendingOtaTargetPatch = target.patch;
    pendingOtaTargetPresent = target.present;
    wsPendingMask |= WSP_WIFI_UPDATE;
    queued = true;
  }
  portEXIT_CRITICAL(&wsPendingMux);
  if (!queued) {
    return false;
  }
#if HDS_ENABLE_ENERGY_MENU
  notifyEnergyMainLoop();
#endif
  return true;
}

inline void remoteFinishWifiUpdateDispatch() {
  portENTER_CRITICAL(&wsPendingMux);
  pendingOtaDispatching = false;
  portEXIT_CRITICAL(&wsPendingMux);
}

inline void wsQueuePending(uint32_t bits) {
  remoteQueuePending(bits);
}

inline void wsReplacePending(uint32_t setBits, uint32_t clearBits) {
  remoteReplacePending(setBits, clearBits);
}

void processWsPendingCmds() {
  if (wsPendingMask == 0) return;
  portENTER_CRITICAL(&wsPendingMux);
  uint32_t mask = b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask;
  uint8_t samplesInUse = pendingSamplesInUse;
  PullOtaTargetVersion otaTarget;
  otaTarget.major = pendingOtaTargetMajor;
  otaTarget.minor = pendingOtaTargetMinor;
  otaTarget.patch = pendingOtaTargetPatch;
  otaTarget.present = pendingOtaTargetPresent;
  unsigned long resetAt = pendingResetAt;
  unsigned long otaResetAt = pendingOtaResetAt;
  wsPendingMask &= ~mask;
  if (mask & WSP_WIFI_UPDATE) {
    pendingOtaDispatching = true;
  }
  if (mask & WSP_RESET) {
    pendingResetAt = 0;
  }
  if (mask & WSP_OTA_RESET) {
    pendingOtaResetAt = 0;
  }
  portEXIT_CRITICAL(&wsPendingMux);
  if (mask == 0) return;

  std::lock_guard<std::mutex> otaDispatchLock(otaDispatchMutex);
  bool droppedWifiUpdate = false;
  portENTER_CRITICAL(&wsPendingMux);
  if (b_ota) {
    droppedWifiUpdate = (mask & WSP_WIFI_UPDATE) != 0;
    uint32_t deferredMask = mask & ~WSP_OTA_RESET;
    const uint32_t replacementGroups[] = {
      WSP_DISPLAY_ON | WSP_DISPLAY_OFF,
      WSP_LOWPWR_ON | WSP_LOWPWR_OFF,
      WSP_SLEEP_ON | WSP_SLEEP_OFF,
      WSP_TIMER_START | WSP_TIMER_STOP | WSP_TIMER_ZERO,
    };
    for (const uint32_t group : replacementGroups) {
      if (wsPendingMask & group) {
        deferredMask &= ~group;
      }
    }
    if ((deferredMask & WSP_RESET) && !(wsPendingMask & WSP_RESET)) {
      pendingResetAt = resetAt;
    }
    wsPendingMask |= deferredMask & ~WSP_WIFI_UPDATE;
    if (mask & WSP_WIFI_UPDATE) {
      pendingOtaDispatching = false;
    }
    mask &= WSP_OTA_RESET;
  }
  portEXIT_CRITICAL(&wsPendingMux);
  if (droppedWifiUpdate) {
    Serial.println("WiFi OTA start dropped; an update is already running.");
  }
  if (mask == 0) return;

  if (mask & WSP_OTA_RESET) {
    if (otaResetAt != 0 && (long)(millis() - otaResetAt) < 0) {
      portENTER_CRITICAL(&wsPendingMux);
      if (!(wsPendingMask & WSP_OTA_RESET)) {
        pendingOtaResetAt = otaResetAt;
      }
      wsPendingMask |= WSP_OTA_RESET;
      portEXIT_CRITICAL(&wsPendingMux);
      mask &= ~WSP_OTA_RESET;
      if (mask == 0) return;
    } else {
      reset();
      return;
    }
  }
  if (mask & WSP_RESET) {
    if (resetAt != 0 && (long)(millis() - resetAt) < 0) {
      portENTER_CRITICAL(&wsPendingMux);
      if (!(wsPendingMask & WSP_RESET)) {
        pendingResetAt = resetAt;
      }
      wsPendingMask |= WSP_RESET;
      portEXIT_CRITICAL(&wsPendingMux);
      mask &= ~WSP_RESET;
      if (mask == 0) return;
    } else {
      reset();
      return;
    }
  }
#if HDS_ENABLE_ENERGY_MENU
  if (mask & WSP_DISPLAY_ON)  { applyEnergyDisplayCommand(true); }
  if (mask & WSP_DISPLAY_OFF) { applyEnergyDisplayCommand(false); }
  if (mask & (WSP_LOWPWR_ON | WSP_LOWPWR_OFF)) { applyEnergyLowPowerCommand(); }
#else
  if (mask & WSP_DISPLAY_ON)  { u8g2.setPowerSave(0); }
  if (mask & WSP_DISPLAY_OFF) { u8g2.setPowerSave(1); }
  if (mask & WSP_LOWPWR_ON)   { u8g2.setContrast(0); }
  if (mask & WSP_LOWPWR_OFF)  { u8g2.setContrast(255); }
#endif
  if (mask & WSP_SLEEP_OFF) {
    if (b_softSleep) {
      wakeScaleFromSoftSleep("remote soft wake");
    } else {
      u8g2.setPowerSave(0);
      b_u8g2Sleep = false;
    }
  }
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
  if ((mask & WSP_BLE_GYRO) && !(mask & WSP_SLEEP_ON) && !b_softSleep) {
    sendBleGyro();
  }
#endif
  if (mask & WSP_SLEEP_ON) {
    b_softSleep = true;
    b_u8g2Sleep = true;
    u8g2.setPowerSave(1);
    digitalWrite(PWR_CTRL, LOW);
    digitalWrite(ACC_PWR_CTRL, LOW);
#if HDS_ENABLE_ENERGY_MENU
    refreshEnergyIdleWakeForRuntimeState();
#endif
  }
  if (mask & WSP_TIMER_START) {
    stopWatch.reset();
    stopWatch.start();
#if HDS_ENABLE_ENERGY_MENU
    recordEnergyActivity();
#endif
  }
  if (mask & WSP_TIMER_STOP)  {
    stopWatch.stop();
#if HDS_ENABLE_ENERGY_MENU
    recordEnergyActivity();
#endif
  }
  if (mask & WSP_TIMER_ZERO)  {
    stopWatch.reset();
#if HDS_ENABLE_ENERGY_MENU
    recordEnergyActivity();
#endif
  }
  if (mask & WSP_SET_SAMPLES) {
    if (setScaleSamplesInUseWhenReady(samplesInUse, "remote samples")) {
      Serial.print("Samples in use set to: ");
      Serial.println(scale.getSamplesInUse());
    } else {
      Serial.println("Samples in use refresh failed");
    }
  }
  if (mask & WSP_WIFI_UPDATE) {
#if HDS_FEATURE_PULL_OTA
    wifiUpdate(otaTarget);
#endif
    remoteFinishWifiUpdateDispatch();
  }
  if (b_ota) {
    remoteQueuePending(mask & WSP_POWER_OFF);
    return;
  }
  if (mask & WSP_POWER_OFF) {
    b_powerOff = true;
  }
}

#if HDS_FEATURE_WEBSOCKET
static const uint32_t WS_BROADCAST_HEAP_FLOOR = 32000;
static const size_t WS_CONTROL_MAX_FRAME_BYTES = 512;
static uint32_t g_wsBroadcastHeapSkips = 0;
static inline bool wsBroadcastHeapOk() {
  if (ESP.getFreeHeap() >= WS_BROADCAST_HEAP_FLOOR) return true;
  g_wsBroadcastHeapSkips++;
  static unsigned long lastLog = 0;
  unsigned long now = millis();
  if (now - lastLog >= 2000) {
    lastLog = now;
    Serial.printf("[ws] low heap %lu < %lu -> skip broadcast (total skips=%lu)\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)WS_BROADCAST_HEAP_FLOOR,
                  (unsigned long)g_wsBroadcastHeapSkips);
  }
  return false;
}

static uint32_t g_wsClientHeapSkips = 0;
static inline bool wsClientHeapOk() {
  if (ESP.getFreeHeap() >= WS_BROADCAST_HEAP_FLOOR) return true;
  g_wsClientHeapSkips++;
  static unsigned long lastLog = 0;
  unsigned long now = millis();
  if (now - lastLog >= 2000) {
    lastLog = now;
    Serial.printf("[ws] low heap %lu < %lu -> skip client reply (total skips=%lu)\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)WS_BROADCAST_HEAP_FLOOR,
                  (unsigned long)g_wsClientHeapSkips);
  }
  return false;
}

void sendWebsocketButton(int buttonNumber, int buttonShortPress) {
  if (!b_wifiEnabled || !b_websocketEventsEnabled || !websocketHasClients()) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"type\":\"button\",\"button\":\"%s\",\"button_number\":%d,\"press\":\"%s\",\"press_code\":%d,\"ms\":%lu}",
                      websocketButtonName(buttonNumber),
                      buttonNumber,
                      websocketPressName(buttonShortPress),
                      buttonShortPress,
                      millis());
}

void sendWebsocketPowerOff(int i_reason) {
  if (!b_wifiEnabled || !b_websocketEventsEnabled || !websocketHasClients()) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"type\":\"power\",\"event\":\"power_off\",\"reason\":\"%s\",\"reason_code\":%d,\"ms\":%lu}",
                      websocketPowerOffReason(i_reason),
                      i_reason,
                      millis());
}

void sendWebsocketRateInfo(AsyncWebSocketClient *client, const char *status) {
  if (!wsClientHeapOk()) return;
  unsigned long hz = websocketRateForInterval(weightWebsocketNotifyInterval);
  client->printf("{\"type\":\"rate\",\"status\":\"%s\",\"interval_ms\":%lu,\"hz\":%lu,\"supported_hz\":[2,5,10]}",
                 status,
                 weightWebsocketNotifyInterval,
                 hz);
}

const char *websocketWifiModeName() {
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_STA) {
    return "sta";
  }
  if (mode == WIFI_AP) {
    return "ap";
  }
  if (mode == WIFI_AP_STA) {
    return "sta_ap";
  }
  return "off";
}

void sendWebsocketStatus(AsyncWebSocketClient *client, const char *status) {
  if (!wsClientHeapOk()) return;
  client->printf("{\"type\":\"status\",\"status\":\"%s\",\"protocol_version\":1,\"firmware_version\":\"%s\",\"grams\":%.2f,\"ms\":%lu,\"battery_percent\":%d,\"battery_voltage\":%.2f,\"charging\":%s,\"timer_running\":%s,\"timer_seconds\":%lu,\"display_on\":%s,\"low_power\":%s,\"soft_sleep\":%s,\"events_enabled\":%s,\"rate_hz\":%lu,\"interval_ms\":%lu,\"wifi_on_boot\":%s,\"wifi_active\":%s,\"wifi_connected\":%s,\"wifi_mode\":\"%s\",\"wifi_credentials_saved\":%s,\"mdns_name\":\"%s\",\"ble_enabled\":%s,\"ble_connected\":%s,\"ble_buttons_enabled\":%s,\"ble_heartbeat_required\":%s,\"auto_sleep_enabled\":%s,\"auto_sleep_minutes\":15,\"quick_boot_enabled\":%s,\"time_on_top\":%s,\"drift_compensation_max_grams\":%.3f}",
                 status,
                 FIRMWARE_VER,
                 f_displayedValue,
                 millis(),
                 websocketBatteryPercent(),
                 f_batteryVoltage,
                 websocketIsCharging() ? "true" : "false",
                 g_timerRunning ? "true" : "false",
                 g_timerElapsed,
                 b_u8g2Sleep ? "false" : "true",
                 b_websocketLowPowerEnabled ? "true" : "false",
                 b_softSleep ? "true" : "false",
                 b_websocketEventsEnabled ? "true" : "false",
                 websocketRateForInterval(weightWebsocketNotifyInterval),
                 weightWebsocketNotifyInterval,
                 b_wifiOnBoot ? "true" : "false",
                 b_wifiEnabled ? "true" : "false",
                 WiFi.status() == WL_CONNECTED ? "true" : "false",
                 websocketWifiModeName(),
                 wifiCredentialsSaved() ? "true" : "false",
                 wifiDeviceName(),
                 b_ble_enabled ? "true" : "false",
                 deviceConnected ? "true" : "false",
                 b_btnFuncWhileConnected ? "true" : "false",
                 b_requireHeartBeat ? "true" : "false",
                 b_autoSleep ? "true" : "false",
                 b_quickBoot ? "true" : "false",
                 b_timeOnTop ? "true" : "false",
                 f_maxDriftCompensation);
}

void sendWebsocketWeightAll(float grams, unsigned long ms) {
  if (!b_wifiEnabled || !websocketHasClients()) return;
  if (!wsBroadcastHeapOk()) return;
  char message[96];
  int messageLength = snprintf(message, sizeof(message),
                               "{\"grams\":%.2f,\"ms\":%lu}", grams, ms);
  if (messageLength <= 0 ||
      static_cast<size_t>(messageLength) >= sizeof(message)) return;
  websocket.textAll(message, static_cast<size_t>(messageLength));
}

void sendWebsocketError(AsyncWebSocketClient *client, const char *code, const char *message) {
  if (!wsClientHeapOk()) return;
  client->printf("{\"type\":\"error\",\"code\":\"%s\",\"message\":\"%s\",\"ms\":%lu}",
                 code,
                 message,
                 millis());
}

bool setWebsocketRateFromInterval(AsyncWebSocketClient *client, unsigned long intervalMs) {
  if (websocketRateForInterval(intervalMs) == 0) {
    sendWebsocketRateInfo(client, "invalid");
    return false;
  }
  weightWebsocketNotifyInterval = intervalMs;
#if HDS_ENABLE_ENERGY_MENU
  recordEnergyActivity();
#endif
  Serial.print("Websocket notify interval set to ");
  Serial.print(weightWebsocketNotifyInterval);
  Serial.println(" ms");
  sendWebsocketRateInfo(client, "ok");
  return true;
}

bool setWebsocketRateFromHz(AsyncWebSocketClient *client, float hz) {
  unsigned long intervalMs = websocketIntervalForRate(hz);
  if (intervalMs == 0) {
    sendWebsocketRateInfo(client, "invalid");
    return false;
  }
  return setWebsocketRateFromInterval(client, intervalMs);
}

bool setWebsocketRateFromValue(AsyncWebSocketClient *client, std::string_view value) {
  unsigned long intervalMs = websocketIntervalForLabel(value);
  if (intervalMs > 0) {
    return setWebsocketRateFromInterval(client, intervalMs);
  }
  return setWebsocketRateFromHz(client, websocketFloatValue(value));
}

bool handleWebsocketControlCommand(
    AsyncWebSocketClient *client,
    std::string_view command,
    std::string_view action = "") {
  command = websocketTrimView(command);
  action = websocketTrimView(action);

#if HDS_ENABLE_ENERGY_MENU
  const bool onOff = websocketEqualsIgnoreCase(action, "on") ||
                     websocketEqualsIgnoreCase(action, "off");
  const bool eventAction = onOff ||
                           websocketEqualsIgnoreCase(action, "enable") ||
                           websocketEqualsIgnoreCase(action, "enabled") ||
                           websocketEqualsIgnoreCase(action, "disable") ||
                           websocketEqualsIgnoreCase(action, "disabled");
  const bool timerAction = websocketEqualsIgnoreCase(action, "start") ||
                           websocketEqualsIgnoreCase(action, "stop") ||
                           websocketEqualsIgnoreCase(action, "zero") ||
                           websocketEqualsIgnoreCase(action, "reset");
  const bool sleepAction = onOff || websocketEqualsIgnoreCase(action, "wake");
  if (websocketEqualsIgnoreCase(command, "tare") ||
      (websocketEqualsIgnoreCase(command, "events") && eventAction) ||
      (websocketEqualsIgnoreCase(command, "timer") && timerAction) ||
      (websocketEqualsIgnoreCase(command, "display") && onOff) ||
      (websocketEqualsIgnoreCase(command, "low_power") && onOff) ||
      ((websocketEqualsIgnoreCase(command, "sleep") ||
        websocketEqualsIgnoreCase(command, "soft_sleep")) && sleepAction) ||
      (websocketEqualsIgnoreCase(command, "power") &&
       websocketEqualsIgnoreCase(action, "off"))
#if HDS_FEATURE_PULL_OTA
      || websocketEqualsIgnoreCase(command, "wifi_update")
#endif
      ) {
    recordEnergyActivity();
  }
#endif

  if (websocketEqualsIgnoreCase(command, "status") ||
      websocketEqualsIgnoreCase(command, "battery") ||
      websocketEqualsIgnoreCase(command, "info")) {
    sendWebsocketStatus(client, "ok");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "events")) {
    if (websocketEqualsIgnoreCase(action, "on") ||
        websocketEqualsIgnoreCase(action, "enable") ||
        websocketEqualsIgnoreCase(action, "enabled")) {
      b_websocketEventsEnabled = true;
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "off") ||
        websocketEqualsIgnoreCase(action, "disable") ||
        websocketEqualsIgnoreCase(action, "disabled")) {
      b_websocketEventsEnabled = false;
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "tare")) {
    requestRemoteTare();
    sendWebsocketStatus(client, "ok");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "timer")) {
    if (websocketEqualsIgnoreCase(action, "start")) {
      Serial.println("Websocket timer start detected.");
      wsReplacePending(WSP_TIMER_START, WSP_TIMER_STOP | WSP_TIMER_ZERO);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "stop")) {
      Serial.println("Websocket timer stop detected.");
      wsReplacePending(WSP_TIMER_STOP, WSP_TIMER_START | WSP_TIMER_ZERO);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "zero") ||
        websocketEqualsIgnoreCase(action, "reset")) {
      Serial.println("Websocket timer zero detected.");
      wsReplacePending(WSP_TIMER_ZERO, WSP_TIMER_START | WSP_TIMER_STOP);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "display")) {
    if (websocketEqualsIgnoreCase(action, "on")) {
      Serial.println("Websocket display on detected.");
#if HDS_ENABLE_ENERGY_MENU
      requestEnergyDisplay(true);
#else
      b_u8g2Sleep = false;
#endif
      wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "off")) {
      Serial.println("Websocket display off detected.");
#if HDS_ENABLE_ENERGY_MENU
      requestEnergyDisplay(false);
#else
      b_u8g2Sleep = true;
#endif
      wsReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "low_power")) {
    if (websocketEqualsIgnoreCase(action, "on")) {
      Serial.println("Websocket low power mode on detected.");
#if HDS_ENABLE_ENERGY_MENU
      requestEnergyLowPower(true);
#else
      b_websocketLowPowerEnabled = true;
#endif
      wsReplacePending(WSP_LOWPWR_ON, WSP_LOWPWR_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "off")) {
      Serial.println("Websocket low power mode off detected.");
#if HDS_ENABLE_ENERGY_MENU
      requestEnergyLowPower(false);
#else
      b_websocketLowPowerEnabled = false;
#endif
      wsReplacePending(WSP_LOWPWR_OFF, WSP_LOWPWR_ON);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "sleep") ||
      websocketEqualsIgnoreCase(command, "soft_sleep")) {
    if (websocketEqualsIgnoreCase(action, "on")) {
      Serial.println("Websocket soft sleep on detected.");
      wsReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (websocketEqualsIgnoreCase(action, "off") ||
        websocketEqualsIgnoreCase(action, "wake")) {
      Serial.println("Websocket soft sleep off detected.");
#if HDS_ENABLE_ENERGY_MENU
      wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);
#else
      const bool wasSoftSleep = b_softSleep;
      b_softSleep = false;
      b_u8g2Sleep = false;
      if (wasSoftSleep) {
        wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);
      } else {
        wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
      }
#endif
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (websocketEqualsIgnoreCase(command, "power") &&
      websocketEqualsIgnoreCase(action, "off")) {
    Serial.println("Websocket power off detected.");
    sendWebsocketPowerOff(0);
    wsQueuePending(WSP_POWER_OFF);
    sendWebsocketStatus(client, "ok");
    return true;
  }

#if HDS_FEATURE_PULL_OTA
  if (websocketEqualsIgnoreCase(command, "wifi_update")) {
    if (b_pullOtaRunning || b_ota) {
      sendWebsocketError(client, "ota_busy", "an update is already running");
      return true;
    }
    PullOtaTargetVersion target = pullOtaNoTargetVersion();
    if (!action.empty() &&
        !pullOtaParseTargetVersion(action.data(), action.size(), target)) {
      sendWebsocketError(client, "ota_version_invalid",
                         "version must be major.minor.patch");
      return true;
    }
    if (!remoteQueueWifiUpdate(target)) {
      sendWebsocketError(client, "ota_busy", "a start request is already queued");
      return true;
    }
    Serial.println("Websocket WiFi update queued.");
    sendWebsocketStatus(client, "ok");
    return true;
  }
#endif

  return false;
}

bool handleWebsocketTextCommand(AsyncWebSocketClient *client, std::string_view msg) {
  msg = websocketTrimView(msg);

  if (websocketEqualsIgnoreCase(msg, "tare")) {
    requestRemoteTare();
    return true;
  }

  if (handleWebsocketControlCommand(client, msg)) {
    return true;
  }

  const char *space = static_cast<const char *>(memchr(msg.data(), ' ', msg.size()));
  if (space != nullptr && space != msg.data() &&
      handleWebsocketControlCommand(
          client,
          msg.substr(0, static_cast<size_t>(space - msg.data())),
          msg.substr(static_cast<size_t>(space - msg.data()) + 1))) {
    return true;
  }

  if (websocketEqualsIgnoreCase(msg, "rate") ||
      websocketEqualsIgnoreCase(msg, "get_rate")) {
    sendWebsocketRateInfo(client, "ok");
    return true;
  }

  if (websocketStartsWithIgnoreCase(msg, "rate ")) {
    std::string_view value = msg.substr(5);
    unsigned long intervalMs = websocketIntervalForLabel(value);
    if (intervalMs > 0) {
      return setWebsocketRateFromInterval(client, intervalMs);
    }
    return setWebsocketRateFromHz(client, websocketFloatValue(value));
  }

  if (websocketStartsWithIgnoreCase(msg, "interval ")) {
    return setWebsocketRateFromInterval(client, websocketUnsignedValue(msg.substr(9)));
  }

  return false;
}

bool handleWebsocketNamedJsonCommand(AsyncWebSocketClient *client, JsonDocument &doc, const char *key) {
  if (!doc[key].is<const char *>()) {
    return false;
  }

  std::string_view command = websocketTrimView(doc[key].as<const char *>());
  const char *action = doc["action"].is<const char *>()
                           ? doc["action"].as<const char *>()
                           : "";

  if (websocketEqualsIgnoreCase(command, "rate") && doc["value"].is<const char *>()) {
    return setWebsocketRateFromValue(client, doc["value"].as<const char *>());
  }

  return handleWebsocketControlCommand(client, command, action);
}

bool handleWebsocketRateCommand(AsyncWebSocketClient *client, uint8_t *data, size_t len) {
  std::string_view msg(reinterpret_cast<const char *>(data), len);
  msg = websocketTrimView(msg);
  if (msg.empty()) {
    return false;
  }
  if (msg.front() != '{') {
    return handleWebsocketTextCommand(client, msg);
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    return false;
  }

  if (doc["rate_hz"].is<float>()) {
    return setWebsocketRateFromHz(client, doc["rate_hz"].as<float>());
  }

  if (doc["hz"].is<float>()) {
    return setWebsocketRateFromHz(client, doc["hz"].as<float>());
  }

  if (doc["rate"].is<const char *>()) {
    unsigned long intervalMs = websocketIntervalForLabel(doc["rate"].as<const char *>());
    if (intervalMs > 0) {
      return setWebsocketRateFromInterval(client, intervalMs);
    }
  }

  if (doc["interval_ms"].is<unsigned long>()) {
    return setWebsocketRateFromInterval(client, doc["interval_ms"].as<unsigned long>());
  }

  static const char *kControlKeys[] = {
      "events", "tare", "timer", "display", "low_power", "sleep", "soft_sleep", "power"};
  for (const char *key : kControlKeys) {
    if (doc[key].is<const char *>()) {
      return handleWebsocketControlCommand(client, key, doc[key].as<const char *>());
    }
    if (doc[key].is<bool>() && strcmp(key, "power") != 0) {
      return handleWebsocketControlCommand(client, key, doc[key].as<bool>() ? "on" : "off");
    }
  }

  if (handleWebsocketNamedJsonCommand(client, doc, "command")) {
    return true;
  }

  if (handleWebsocketNamedJsonCommand(client, doc, "cmd")) {
    return true;
  }

  return false;
}

void setupWebsocketEvents() {
  websocket.onEvent([](
                      AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
#if HDS_ENABLE_ENERGY_MENU
      recordEnergyActivity();
#endif
      server->cleanupClients(4);
      if (server->count() > 4) {
        client->close();
      }
      g_websocketClientCount.store(server->count(), std::memory_order_relaxed);
      Serial.printf("Client %u connected\n", client->id());
      client->setCloseClientOnQueueFull(false);
      client->client()->setAckTimeout(30000);
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("Client %u disconnected\n", client->id());
      g_websocketClientCount.store(server->count(), std::memory_order_relaxed);
      if (!websocketHasClients()) {
        weightWebsocketNotifyInterval = WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS;
        b_websocketEventsEnabled = false;
      }
    } else if (type == WS_EVT_ERROR) {
      Serial.printf("WebSocket error on client %u: code=%u reason=%.*s\n",
                    client->id(), arg ? *((uint16_t *)arg) : 0,
                    (int)len, (len && data) ? (const char *)data : "");
    } else if (type == WS_EVT_PONG) {
      Serial.printf("Pong received from client %u\n", client->id());
    }
    if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        if (info->len > WS_CONTROL_MAX_FRAME_BYTES) {
          sendWebsocketError(client, "frame_too_large", "control frame too large");
          return;
        }
        Serial.printf("Websocket recv: %.*s\n", static_cast<int>(len),
                      reinterpret_cast<const char *>(data));
        if (!handleWebsocketRateCommand(client, data, len)) {
          sendWebsocketError(client, "unknown_command",
                             "unrecognized or malformed command");
        }
      }
    }
  });
}
#endif
#endif
