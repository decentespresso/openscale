#ifndef WEBSOCKET_H
#define WEBSOCKET_H
#include "declare.h"
#include "parameter.h"
#include "power.h"
#include "display.h"
#include "wifi_setup.h"
#include "webserver.h"

void wifiUpdate();
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif

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

unsigned long websocketIntervalForLabel(String label) {
  label.trim();
  label.toLowerCase();
  if (label == "2" || label == "2hz" || label == "2k") {
    return WEBSOCKET_2HZ_NOTIFY_INTERVAL_MS;
  }
  if (label == "5" || label == "5hz" || label == "5k") {
    return WEBSOCKET_5HZ_NOTIFY_INTERVAL_MS;
  }
  if (label == "10" || label == "10hz" || label == "10k") {
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

inline void remoteQueuePending(uint32_t bits) {
  portENTER_CRITICAL(&wsPendingMux);
  if (bits & WSP_RESET) {
    pendingResetAt = 0;
  }
  wsPendingMask |= bits;
  portEXIT_CRITICAL(&wsPendingMux);
}

inline void remoteReplacePending(uint32_t setBits, uint32_t clearBits) {
  portENTER_CRITICAL(&wsPendingMux);
  wsPendingMask = (wsPendingMask & ~clearBits) | setBits;
  portEXIT_CRITICAL(&wsPendingMux);
}

inline void remoteQueueSamplesInUse(uint8_t samplesInUse) {
  portENTER_CRITICAL(&wsPendingMux);
  pendingSamplesInUse = samplesInUse;
  wsPendingMask |= WSP_SET_SAMPLES;
  portEXIT_CRITICAL(&wsPendingMux);
}

inline void wsQueuePending(uint32_t bits) {
  remoteQueuePending(bits);
}

inline void wsReplacePending(uint32_t setBits, uint32_t clearBits) {
  remoteReplacePending(setBits, clearBits);
}

void processWsPendingCmds() {
  portENTER_CRITICAL(&wsPendingMux);
  uint32_t mask = wsPendingMask;
  uint8_t samplesInUse = pendingSamplesInUse;
  unsigned long resetAt = pendingResetAt;
  wsPendingMask = 0;
  if (mask & WSP_RESET) {
    pendingResetAt = 0;
  }
  portEXIT_CRITICAL(&wsPendingMux);
  if (mask == 0) return;

  if (mask & WSP_RESET) {
    if (resetAt != 0 && (long)(millis() - resetAt) < 0) {
      portENTER_CRITICAL(&wsPendingMux);
      wsPendingMask |= WSP_RESET;
      pendingResetAt = resetAt;
      portEXIT_CRITICAL(&wsPendingMux);
      mask &= ~WSP_RESET;
      if (mask == 0) return;
    } else {
      reset();
      return;
    }
  }
  if (mask & WSP_DISPLAY_ON)  { u8g2.setPowerSave(0); }
  if (mask & WSP_DISPLAY_OFF) { u8g2.setPowerSave(1); }
  if (mask & WSP_LOWPWR_ON)   { u8g2.setContrast(0); }
  if (mask & WSP_LOWPWR_OFF)  { u8g2.setContrast(255); }
  if (mask & WSP_SLEEP_OFF) {
    wakeScaleFromSoftSleep("remote soft wake");
  }
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
  if ((mask & WSP_BLE_GYRO) && !(mask & WSP_SLEEP_ON) && !b_softSleep) {
    sendBleGyro();
  }
#endif
  if (mask & WSP_SLEEP_ON) {
    u8g2.setPowerSave(1);
    digitalWrite(PWR_CTRL, LOW);
    digitalWrite(ACC_PWR_CTRL, LOW);
  }
  if (mask & WSP_TIMER_START) {
    stopWatch.reset();
    stopWatch.start();
  }
  if (mask & WSP_TIMER_STOP)  { stopWatch.stop(); }
  if (mask & WSP_TIMER_ZERO)  { stopWatch.reset(); }
  if (mask & WSP_SET_SAMPLES) {
    if (setScaleSamplesInUseWhenReady(samplesInUse, "remote samples")) {
      Serial.print("Samples in use set to: ");
      Serial.println(scale.getSamplesInUse());
    } else {
      Serial.println("Samples in use refresh failed");
    }
  }
  if (mask & WSP_WIFI_UPDATE) {
    wifiUpdate();
  }
  if (mask & WSP_POWER_OFF) {
    b_powerOff = true;
  }
}

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
  if (!b_wifiEnabled || !b_websocketEventsEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"type\":\"button\",\"button\":\"%s\",\"button_number\":%d,\"press\":\"%s\",\"press_code\":%d,\"ms\":%lu}",
                      websocketButtonName(buttonNumber),
                      buttonNumber,
                      websocketPressName(buttonShortPress),
                      buttonShortPress,
                      millis());
}

void sendWebsocketPowerOff(int i_reason) {
  if (!b_wifiEnabled || !b_websocketEventsEnabled || websocket.count() == 0) return;
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
  if (!b_wifiEnabled || websocket.count() == 0) return;
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

bool setWebsocketRateFromLabel(AsyncWebSocketClient *client, String label) {
  unsigned long intervalMs = websocketIntervalForLabel(label);
  if (intervalMs == 0) {
    sendWebsocketRateInfo(client, "invalid");
    return false;
  }
  return setWebsocketRateFromInterval(client, intervalMs);
}

bool setWebsocketRateFromValue(AsyncWebSocketClient *client, String value) {
  unsigned long intervalMs = websocketIntervalForLabel(value);
  if (intervalMs > 0) {
    return setWebsocketRateFromInterval(client, intervalMs);
  }
  return setWebsocketRateFromHz(client, value.toFloat());
}

bool handleWebsocketControlCommand(AsyncWebSocketClient *client, String command, String action = "") {
  command.trim();
  action.trim();
  command.toLowerCase();
  action.toLowerCase();

  if (command == "status" || command == "battery" || command == "info") {
    sendWebsocketStatus(client, "ok");
    return true;
  }

  if (command == "events") {
    if (action == "on" || action == "enable" || action == "enabled") {
      b_websocketEventsEnabled = true;
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "off" || action == "disable" || action == "disabled") {
      b_websocketEventsEnabled = false;
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (command == "tare") {
    requestRemoteTare();
    sendWebsocketStatus(client, "ok");
    return true;
  }

  if (command == "timer") {
    if (action == "start") {
      Serial.println("Websocket timer start detected.");
      wsReplacePending(WSP_TIMER_START, WSP_TIMER_STOP | WSP_TIMER_ZERO);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "stop") {
      Serial.println("Websocket timer stop detected.");
      wsReplacePending(WSP_TIMER_STOP, WSP_TIMER_START | WSP_TIMER_ZERO);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "zero" || action == "reset") {
      Serial.println("Websocket timer zero detected.");
      wsReplacePending(WSP_TIMER_ZERO, WSP_TIMER_START | WSP_TIMER_STOP);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (command == "display") {
    if (action == "on") {
      Serial.println("Websocket display on detected.");
      b_u8g2Sleep = false;
      wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "off") {
      Serial.println("Websocket display off detected.");
      b_u8g2Sleep = true;
      wsReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (command == "low_power") {
    if (action == "on") {
      Serial.println("Websocket low power mode on detected.");
      b_websocketLowPowerEnabled = true;
      wsReplacePending(WSP_LOWPWR_ON, WSP_LOWPWR_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "off") {
      Serial.println("Websocket low power mode off detected.");
      b_websocketLowPowerEnabled = false;
      wsReplacePending(WSP_LOWPWR_OFF, WSP_LOWPWR_ON);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (command == "sleep" || command == "soft_sleep") {
    if (action == "on") {
      Serial.println("Websocket soft sleep on detected.");
      b_softSleep = true;
      b_u8g2Sleep = true;
      wsReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "off" || action == "wake") {
      Serial.println("Websocket soft sleep off detected.");
      bool wasSoftSleep = b_softSleep;
      b_softSleep = false;
      b_u8g2Sleep = false;
      if (wasSoftSleep) {
        wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);
      } else {
        wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
      }
      sendWebsocketStatus(client, "ok");
      return true;
    }
    sendWebsocketStatus(client, "invalid");
    return true;
  }

  if (command == "power" && action == "off") {
    Serial.println("Websocket power off detected.");
    sendWebsocketPowerOff(0);
    wsQueuePending(WSP_POWER_OFF);
    sendWebsocketStatus(client, "ok");
    return true;
  }

  return false;
}

bool handleWebsocketTextCommand(AsyncWebSocketClient *client, String msg) {
  msg.trim();
  String lowerMsg = msg;
  lowerMsg.toLowerCase();

  if (lowerMsg == "tare") {
    requestRemoteTare();
    return true;
  }

  if (handleWebsocketControlCommand(client, lowerMsg)) {
    return true;
  }

  int spaceIndex = lowerMsg.indexOf(' ');
  if (spaceIndex > 0 &&
      handleWebsocketControlCommand(client,
                                    lowerMsg.substring(0, spaceIndex),
                                    lowerMsg.substring(spaceIndex + 1))) {
    return true;
  }

  if (lowerMsg == "rate" || lowerMsg == "get_rate") {
    sendWebsocketRateInfo(client, "ok");
    return true;
  }

  if (lowerMsg.startsWith("rate ")) {
    unsigned long intervalMs = websocketIntervalForLabel(msg.substring(5));
    if (intervalMs > 0) {
      return setWebsocketRateFromInterval(client, intervalMs);
    }
    return setWebsocketRateFromHz(client, msg.substring(5).toFloat());
  }

  if (lowerMsg.startsWith("interval ")) {
    return setWebsocketRateFromInterval(client, msg.substring(9).toInt());
  }

  return false;
}

bool handleWebsocketNamedJsonCommand(AsyncWebSocketClient *client, JsonDocument &doc, const char *key) {
  if (!doc[key].is<const char *>()) {
    return false;
  }

  String command = doc[key].as<String>();
  command.trim();
  command.toLowerCase();

  String action = "";
  if (doc["action"].is<const char *>()) {
    action = doc["action"].as<String>();
  }

  if (command == "rate" && doc["value"].is<const char *>()) {
    return setWebsocketRateFromValue(client, doc["value"].as<String>());
  }

  return handleWebsocketControlCommand(client, command, action);
}

bool handleWebsocketRateCommand(AsyncWebSocketClient *client, String msg) {
  msg.trim();
  String lowerMsg = msg;
  lowerMsg.toLowerCase();

  if (!msg.startsWith("{")) {
    return handleWebsocketTextCommand(client, msg);
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, msg);
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
    unsigned long intervalMs = websocketIntervalForLabel(doc["rate"].as<String>());
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
      return handleWebsocketControlCommand(client, key, doc[key].as<String>());
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
      Serial.printf("Client %u connected\n", client->id());
      client->setCloseClientOnQueueFull(false);
      client->client()->setAckTimeout(30000);
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("Client %u disconnected\n", client->id());
      if (server->count() == 0) {
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
        String msg((const char *)data, len);
        Serial.print("Websocket recv: ");
        Serial.println(msg);
        if (!handleWebsocketRateCommand(client, msg)) {
          sendWebsocketError(client, "unknown_command",
                             "unrecognized or malformed command");
        }
      }
    }
  });
}
#endif
