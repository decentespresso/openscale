#ifndef WEBSOCKET_H
#define WEBSOCKET_H
#include "declare.h"
#include "parameter.h"
#include "power.h"
#include "display.h"
#include "wifi_setup.h"
#include "webserver.h"

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

// Queue a pending hardware action so the main loop performs it on its own
// task. The WS event callback runs on the AsyncTCP task and must not touch
// u8g2 (I2C bus) or peripheral power-rail GPIOs directly. StopWatch and
// plain bool/uint32 flag writes are safe to do inline in the callback.
inline void wsQueuePending(uint32_t bits) {
  portENTER_CRITICAL(&wsPendingMux);
  wsPendingMask |= bits;
  portEXIT_CRITICAL(&wsPendingMux);
}

// Atomically set `setBits` and clear `clearBits` in the pending mask. Used
// for mutually-exclusive action pairs (DISPLAY_ON/OFF, SLEEP_ON/OFF, ...) so
// that if a previous opposing action is still queued, it's superseded by
// the new one rather than running both in source order.
inline void wsReplacePending(uint32_t setBits, uint32_t clearBits) {
  portENTER_CRITICAL(&wsPendingMux);
  wsPendingMask = (wsPendingMask & ~clearBits) | setBits;
  portEXIT_CRITICAL(&wsPendingMux);
}

void processWsPendingCmds() {
  portENTER_CRITICAL(&wsPendingMux);
  uint32_t mask = wsPendingMask;
  wsPendingMask = 0;
  portEXIT_CRITICAL(&wsPendingMux);
  if (mask == 0) return;

  // u8g2 (I2C), peripheral power-rail GPIOs, and the StopWatch timer are
  // touched here on the main loop task. Doing them from the AsyncTCP task
  // would race the main loop (I2C/GPIO) or tear a concurrent StopWatch read in
  // the status frame. State flags touched by the user-visible status
  // (b_u8g2Sleep, b_softSleep, ...) are updated synchronously in the WS
  // callback so the response is accurate; b_powerOff is set here so it
  // sequences after the centralized shutdown chain in shut_down_now_nobeep().
  if (mask & WSP_DISPLAY_ON)  { u8g2.setPowerSave(0); }
  if (mask & WSP_DISPLAY_OFF) { u8g2.setPowerSave(1); }
  if (mask & WSP_LOWPWR_ON)   { u8g2.setContrast(0); }
  if (mask & WSP_LOWPWR_OFF)  { u8g2.setContrast(255); }
  if (mask & WSP_SLEEP_OFF) {
    digitalWrite(PWR_CTRL, HIGH);
    digitalWrite(ACC_PWR_CTRL, HIGH);
    u8g2.setPowerSave(0);
  }
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
  if (mask & WSP_POWER_OFF) {
    b_powerOff = true;
  }
}

// --- Heap-floor gate for periodic WS broadcasts ------------------------------
// printfAll() allocates an AsyncWebSocketMessage (a heap buffer) for EVERY
// connected client. Under WebSocket connection churn the heap can collapse, and
// that allocation then throws std::bad_alloc -> std::terminate() -> abort()
// (Arduino-ESP32 builds with -fno-exceptions, so the throw can't be caught) ->
// reboot. That OOM-reboot is the "weight stops being collected under sustained
// multi-client load" failure. Skipping a frame is invisible (the next weight
// frame is <=500 ms away, status <=5 s); crashing is not. The floor sits above
// the 15 KB heap watchdog (wifi_setup.cpp) so broadcasts back off well before a
// reboot is even considered. Every broadcast helper below runs on the main loop,
// so the skip counter needs no synchronization.
static const uint32_t WS_BROADCAST_HEAP_FLOOR = 25000;
static uint32_t g_wsBroadcastHeapSkips = 0;
static inline bool wsBroadcastHeapOk() {
  if (ESP.getFreeHeap() >= WS_BROADCAST_HEAP_FLOOR) return true;
  g_wsBroadcastHeapSkips++;
  static unsigned long lastLog = 0;
  unsigned long now = millis();
  if (now - lastLog >= 2000) {  // rate-limit: broadcasts can be 10 Hz
    lastLog = now;
    Serial.printf("[ws] low heap %lu < %lu -> skip broadcast (total skips=%lu)\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)WS_BROADCAST_HEAP_FLOOR,
                  (unsigned long)g_wsBroadcastHeapSkips);
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
  unsigned long hz = websocketRateForInterval(weightWebsocketNotifyInterval);
  client->printf("{\"type\":\"rate\",\"status\":\"%s\",\"interval_ms\":%lu,\"hz\":%lu,\"supported_hz\":[2,5,10]}",
                 status,
                 weightWebsocketNotifyInterval,
                 hz);
}

void sendWebsocketStatus(AsyncWebSocketClient *client, const char *status) {
  // protocol_version + firmware_version moved to session_info (sent once per
  // client on connect); reset_reason + the diagnostic telemetry moved to debug.
  // Keeping status lean: only fields whose value actually changes during a
  // session belong here.
  client->printf("{\"type\":\"status\",\"status\":\"%s\",\"grams\":%.2f,\"ms\":%lu,\"battery_percent\":%d,\"battery_voltage\":%.2f,\"charging\":%s,\"timer_running\":%s,\"timer_seconds\":%lu,\"display_on\":%s,\"low_power\":%s,\"soft_sleep\":%s,\"events_enabled\":%s,\"rate_hz\":%lu,\"interval_ms\":%lu}",
                 status,
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
                 weightWebsocketNotifyInterval);
}

// Broadcast via printfAll(): it holds the library's client-list mutex and
// queues to each client independently. We deliberately do NOT gate on
// availableForWriteAll() (it returns the minimum across clients, coupling every
// client to the slowest), and we deliberately do NOT hand-iterate getClients()
// (that accessor doesn't take the mutex, so iterating on the loop task would
// race client disconnects on the AsyncTCP task). With
// setCloseClientOnQueueFull(false), a backed-up client drops its own frame
// without blocking the others.
void sendWebsocketStatusAll(const char *status) {
  if (!b_wifiEnabled || !b_websocketEventsEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  // See sendWebsocketStatus() above for the rationale on the lean shape.
  websocket.printfAll("{\"type\":\"status\",\"status\":\"%s\",\"grams\":%.2f,\"ms\":%lu,\"battery_percent\":%d,\"battery_voltage\":%.2f,\"charging\":%s,\"timer_running\":%s,\"timer_seconds\":%lu,\"display_on\":%s,\"low_power\":%s,\"soft_sleep\":%s,\"events_enabled\":%s,\"rate_hz\":%lu,\"interval_ms\":%lu}",
                      status,
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
                      weightWebsocketNotifyInterval);
}

void sendWebsocketWeightAll(float grams, unsigned long ms) {
  if (!b_wifiEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"grams\":%.2f,\"ms\":%lu}", grams, ms);
}

// --- Telemetry delivery ------------------------------------------------------
//
// Telemetry (SoC temp, weight-stall watchdog, ADC recovery count, reset reason)
// is delivered out-of-band from the hot status frame so that apps which don't
// care about it -- the on-device UI, the decentespresso app, third-party scale
// apps -- aren't paying ~21% extra bytes on every status broadcast. The split:
//
//   session_info  one-shot, sent to each client on WS_EVT_CONNECT. Carries the
//                 fields that don't change for the life of the connection
//                 (protocol_version, firmware_version, reset_reason).
//
//   debug events  broadcast on the *change* that matters: stall_start /
//                 stall_end, adc_recovery (count incremented), temp_peak (new
//                 max). Subscribers see the event the moment it happens; non-
//                 subscribers ignore the unknown type. No periodic broadcast.
//
//   debug reply   on-request snapshot. A client sends {"command":"debug"} and
//                 the server replies with the full diagnostic set (current
//                 SoC temp, max temp, stall state/count/last, recovery count).
//                 Per-client, not a broadcast -- no heap cost for other clients.
//
// All event broadcasts use the same wsBroadcastHeapOk() gate as the other
// printfAll helpers above. Per-client sends (session_info, debug reply) don't
// need the gate since they're one allocation, not one-per-client.
void sendWebsocketSessionInfo(AsyncWebSocketClient *client) {
  client->printf("{\"type\":\"session_info\",\"protocol_version\":1,\"firmware_version\":\"%s\",\"reset_reason\":\"%s\",\"ms\":%lu}",
                 FIRMWARE_VER,
                 (const char *)g_resetReason,
                 millis());
}

void sendWebsocketDebug(AsyncWebSocketClient *client, const char *status) {
  client->printf("{\"type\":\"debug\",\"status\":\"%s\",\"soc_temp_c\":%.1f,\"soc_temp_max_c\":%.1f,\"weight_stalled\":%s,\"stall_count\":%lu,\"last_stall_ms\":%lu,\"last_stall_temp_c\":%.1f,\"adc_recovery_count\":%lu,\"ms\":%lu}",
                 status,
                 g_socTempC,
                 g_socTempMaxC,
                 b_weightStalled ? "true" : "false",
                 (unsigned long)g_stallCount,
                 g_lastStallMs,
                 g_lastStallTempC,
                 (unsigned long)i_adc_recovery_count,
                 millis());
}

// Event broadcasts. Only fields relevant to the event are included -- subscribers
// keep their own running snapshot from session_info + the on-request debug reply,
// and update it from these deltas. Keeps each event small (~80-140 B).
void sendWebsocketDebugStall(bool started) {
  if (!b_wifiEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  if (started) {
    websocket.printfAll("{\"type\":\"debug\",\"event\":\"stall_start\",\"stall_count\":%lu,\"last_stall_ms\":%lu,\"last_stall_temp_c\":%.1f,\"ms\":%lu}",
                        (unsigned long)g_stallCount,
                        g_lastStallMs,
                        g_lastStallTempC,
                        millis());
  } else {
    websocket.printfAll("{\"type\":\"debug\",\"event\":\"stall_end\",\"ms\":%lu}", millis());
  }
}

void sendWebsocketDebugAdcRecovery() {
  if (!b_wifiEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"type\":\"debug\",\"event\":\"adc_recovery\",\"adc_recovery_count\":%lu,\"ms\":%lu}",
                      (unsigned long)i_adc_recovery_count,
                      millis());
}

// temp_peak is broadcast when g_socTempMaxC ticks up. Rate-limited at the call
// site (main loop) -- not here -- since the temp sampler already throttles.
void sendWebsocketDebugTempPeak(float maxC) {
  if (!b_wifiEnabled || websocket.count() == 0) return;
  if (!wsBroadcastHeapOk()) return;
  websocket.printfAll("{\"type\":\"debug\",\"event\":\"temp_peak\",\"soc_temp_max_c\":%.1f,\"ms\":%lu}",
                      maxC,
                      millis());
}

void sendWebsocketError(AsyncWebSocketClient *client, const char *code, const char *message) {
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

// Trust model: these WebSocket commands are unauthenticated, exactly like the
// BLE control interface — any host that can reach /snapshot can drive the
// scale, including destructive commands (`power off`, `sleep`, `display off`).
// The device assumes it is on a trusted LAN; do not expose it to an untrusted
// network.
bool handleWebsocketControlCommand(AsyncWebSocketClient *client, String command, String action = "") {
  command.trim();
  action.trim();
  command.toLowerCase();
  action.toLowerCase();

  if (command == "status" || command == "battery" || command == "info") {
    sendWebsocketStatus(client, "ok");
    return true;
  }

  if (command == "debug" || command == "diag") {
    sendWebsocketDebug(client, "ok");
    return true;
  }

  if (command == "session_info" || command == "session") {
    sendWebsocketSessionInfo(client);
    return true;
  }

  if (command == "events") {
    if (action == "on" || action == "enable" || action == "enabled") {
      b_websocketEventsEnabled = true;
      t_lastWebsocketStatusUpdate = millis();
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
    // StopWatch is multi-field and is also mutated from the main loop, BLE and
    // USB. Instead of touching it from the AsyncTCP task (which could tear a
    // concurrent elapsed()/isRunning() read in the status frame), defer the
    // mutation to the main loop via the pending mask — the same producer/
    // consumer guarding that requestRemoteTare() uses for tares.
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
    // Update the state flag synchronously so the status frame we send next
    // reflects the requested state. The u8g2 call (I2C, not thread-safe) is
    // deferred and runs on the main loop one tick later.
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
      // Set state flags synchronously so status reflects the requested mode.
      // u8g2 + GPIO power-rail writes are deferred to the main loop. Soft sleep
      // turns the OLED off, so mark b_u8g2Sleep too (status derives display_on
      // from it) — mirrors the wake branch below that clears it.
      b_softSleep = true;
      b_u8g2Sleep = true;
      wsReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
      sendWebsocketStatus(client, "ok");
      return true;
    }
    if (action == "off" || action == "wake") {
      Serial.println("Websocket soft sleep off detected.");
      b_softSleep = false;
      b_u8g2Sleep = false;
      wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);
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

  // Shorthand control commands: {"events":"on"}, {"display":"off"},
  // {"tare":true}, etc. -- a control key at top level whose value is the action.
  // (Mirrors the {"rate":"10k"} shorthand above.)
  static const char *kControlKeys[] = {
      "events", "tare", "timer", "display", "low_power", "sleep", "soft_sleep", "power"};
  for (const char *key : kControlKeys) {
    if (doc[key].is<const char *>()) {
      return handleWebsocketControlCommand(client, key, doc[key].as<String>());
    }
    // Bool form: {"display":true/false} -> "on"/"off" (and {"tare":true} fires
    // tare, which ignores the action). Exclude "power": a bool must not be able
    // to trigger power-off -- that requires the explicit {"power":"off"} form.
    if (doc[key].is<bool>() && strcmp(key, "power") != 0) {
      return handleWebsocketControlCommand(client, key, doc[key].as<bool>() ? "on" : "off");
    }
  }

  if (doc["command"].is<const char *>()) {
    String command = doc["command"].as<String>();
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

  if (doc["cmd"].is<const char *>()) {
    String command = doc["cmd"].as<String>();
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

  return false;
}

void setupWebsocketEvents() {
  websocket.onEvent([](
                      AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      Serial.printf("Client %u connected\n", client->id());
      client->setCloseClientOnQueueFull(false);
      // Ride out transient BT/WiFi coexistence stalls. AsyncTCP's default ACK
      // timeout (CONFIG_ASYNC_TCP_MAX_ACK_TIME, 5000 ms in esp32async AsyncTCP
      // ~3.x) closes the socket (graceful FIN, no WS close frame) when a few
      // seconds of sent data go unacked during a radio stall -- the cause of the
      // ~1-2 min "clean disconnect" drops. Raising it lets the stream resume
      // when the radio frees up instead of dropping. A genuinely dead client is
      // still reaped, just later.
      client->client()->setAckTimeout(30000);
      // Send the session-immutable fields (protocol version, firmware version,
      // reset reason) once on connect, so clients don't have to ask -- and so
      // the hot status broadcast doesn't carry them on every tick.
      sendWebsocketSessionInfo(client);
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("Client %u disconnected\n", client->id());
      // Only reset shared session state when the LAST client leaves —
      // otherwise one client disconnecting would wipe rate/events state
      // for any other still-connected clients. server->count() at this
      // point excludes the disconnecting client.
      if (server->count() == 0) {
        weightWebsocketNotifyInterval = WEBSOCKET_DEFAULT_NOTIFY_INTERVAL_MS;
        b_websocketEventsEnabled = false;
        t_lastWebsocketStatusUpdate = 0;
      }
    } else if (type == WS_EVT_ERROR) {
      // arg = reason code (uint16_t*), data/len = human-readable reason. Log
      // both so a protocol error is distinguishable from a network tear.
      Serial.printf("WebSocket error on client %u: code=%u reason=%.*s\n",
                    client->id(), arg ? *((uint16_t *)arg) : 0,
                    (int)len, (len && data) ? (const char *)data : "");
    } else if (type == WS_EVT_PONG) {
      Serial.printf("Pong received from client %u\n", client->id());
    }
    if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      // Only handle complete, unfragmented text frames. Fragmented or binary
      // frames would corrupt the parsers below.
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String msg((const char *)data, len);
        Serial.print("Websocket recv: ");
        Serial.println(msg);
        // Unrecognized or malformed commands get an explicit error frame
        // rather than silence, so a client can tell "rejected" from "frame
        // never arrived". (The legacy bare `tare` string is handled above and
        // stays intentionally silent.)
        if (!handleWebsocketRateCommand(client, msg)) {
          sendWebsocketError(client, "unknown_command",
                             "unrecognized or malformed command");
        }
      }
    }
  });
}
#endif
