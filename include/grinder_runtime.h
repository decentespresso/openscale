#ifndef GRINDER_RUNTIME_H
#define GRINDER_RUNTIME_H

#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <math.h>
#include "grinder_protocol.h"
#include "grinder_adaptive_safety.h"

#ifndef GRINDER_RUNTIME_RECONNECT_INTERVAL_MS
#define GRINDER_RUNTIME_RECONNECT_INTERVAL_MS 3000
#endif

#ifndef GRINDER_RUNTIME_HOST_RESOLVE_TIMEOUT_MS
#define GRINDER_RUNTIME_HOST_RESOLVE_TIMEOUT_MS 250
#endif

enum GrinderDosingState {
  GRINDER_STATE_DISABLED,
  GRINDER_STATE_FINDING_PLUG,
  GRINDER_STATE_CONNECTED,
  GRINDER_STATE_ARMED,
  GRINDER_STATE_GRINDING,
  GRINDER_STATE_STOPPING,
  GRINDER_STATE_AWAIT_REMOVAL,
  GRINDER_STATE_AWAIT_ZERO,
  GRINDER_STATE_ERROR
};

enum GrinderPendingCommand {
  GRINDER_COMMAND_NONE,
  GRINDER_COMMAND_HELLO,
  GRINDER_COMMAND_PING,
  GRINDER_COMMAND_ON,
  GRINDER_COMMAND_OFF,
  GRINDER_COMMAND_BYE
};

struct GrinderSettings {
  bool enabled = false;
  char selectedMac[18] = { 0 };
  char hostname[64] = { 0 };
  IPAddress lastIp = IPAddress((uint32_t)0);
  float targetGrams = 15.0f;
  float safetyMarginGrams = 2.0f;
  float zeroMinGrams = -1.0f;
  float zeroMaxGrams = 1.0f;
  float targetToleranceGrams = 0.5f;
  GrinderAdaptiveSafetyStore adaptiveSafety;
  uint32_t zeroHoldMs = 1000;
  bool previousWifiOnBoot = false;
  bool previousWifiOnBootSaved = false;
};

struct GrinderDiscoveredPlug {
  char mac[18] = { 0 };
  char hostname[64] = { 0 };
  IPAddress ip = IPAddress((uint32_t)0);
};

struct GrinderRuntime {
  WiFiClient client;
  GrinderDosingState state = GRINDER_STATE_DISABLED;
  GrinderPendingCommand pendingCommand = GRINDER_COMMAND_NONE;
  GrinderDiscoveredPlug discovered[8];
  uint8_t discoveredCount = 0;
  uint8_t resolvePhase = 0;
  uint8_t rateSamples = 0;
  char scaleMac[18] = { 0 };
  char activePlugMac[18] = { 0 };
  char status[32] = { 0 };
  char rxLine[GRINDER_TCP_MAX_LINE_LENGTH + 1] = { 0 };
  uint16_t rxLength = 0;
  bool pendingCr = false;
  bool relayOn = false;
  bool removalSeen = false;
  bool zeroTracking = false;
  bool settingsDirty = false;
  bool cutoffGuardActive = false;
  bool tarePending = false;
  bool tareRequestArmsGrinder = false;
  bool userTareComplete = false;
  bool tareRearmRequested = false;
  bool grindConfirmed = false;
  bool grindCandidateActive = false;
  bool setupMassBlocked = false;
  bool menuPaused = false;
  GrinderAdaptiveShot adaptiveShot;
  float zeroTrackWeight = 0.0f;
  float lastWeight = 0.0f;
  float grindRateGps = 0.0f;
  float stopWeight = 0.0f;
  float grindCandidateStartWeight = 0.0f;
  float grindCandidateLastWeight = 0.0f;
  uint32_t lastConnectAttempt = 0;
  uint32_t lastPingAt = 0;
  uint32_t lastRxAt = 0;
  uint32_t lastCommandAt = 0;
  uint32_t zeroStableSince = 0;
  uint32_t lastWeightAt = 0;
  uint32_t stateEnteredAt = 0;
  uint32_t lastRuntimeLogAt = 0;
  uint32_t lastFastWeightSequence = 0;
  uint32_t cutoffGuardZeroExitAt = 0;
  uint32_t grindCandidateStartAt = 0;
  uint8_t grindCandidatePositiveSamples = 0;
  uint8_t missedPingResponses = 0;
};

GrinderSettings grinderSettings;
GrinderRuntime grinderRuntime;

static inline bool grinderIpValid(IPAddress ip) {
  return (uint32_t)ip != 0;
}

static inline void grinderCopyCString(char *output, size_t outputSize, const char *input) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  if (input == nullptr) {
    output[0] = 0;
    return;
  }
  snprintf(output, outputSize, "%s", input);
}

static inline void grinderCopyString(char *output, size_t outputSize, const String &input) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  input.toCharArray(output, outputSize);
  output[outputSize - 1] = 0;
}

static inline bool grinderFiniteInRange(float value, float minValue, float maxValue) {
  return isfinite(value) && value >= minValue && value <= maxValue;
}

static inline void grinderNormalizeSettings() {
  if (!grinderIsMac(grinderSettings.selectedMac)) {
    grinderSettings.selectedMac[0] = 0;
  }
  grinderSettings.targetGrams = grinderNormalizeTargetGrams(grinderSettings.targetGrams);
  grinderSettings.safetyMarginGrams = grinderNormalizeSafetyGrams(grinderSettings.safetyMarginGrams, grinderSettings.targetGrams);
  if (!grinderFiniteInRange(grinderSettings.zeroMinGrams, -20.0f, 0.0f)) {
    grinderSettings.zeroMinGrams = -1.0f;
  }
  if (!grinderFiniteInRange(grinderSettings.zeroMaxGrams, 0.0f, 20.0f)) {
    grinderSettings.zeroMaxGrams = 1.0f;
  }
  if (grinderSettings.zeroMinGrams >= grinderSettings.zeroMaxGrams) {
    grinderSettings.zeroMinGrams = -1.0f;
    grinderSettings.zeroMaxGrams = 1.0f;
  }
  if (!grinderFiniteInRange(grinderSettings.targetToleranceGrams, 0.1f, 10.0f)) {
    grinderSettings.targetToleranceGrams = 0.5f;
  }
  if (grinderSettings.zeroHoldMs < 100 || grinderSettings.zeroHoldMs > 10000) {
    grinderSettings.zeroHoldMs = 1000;
  }
  grinderAdaptiveSafetyNormalize(&grinderSettings.adaptiveSafety,
                                 grinderSettings.safetyMarginGrams,
                                 grinderMaxSafetyGrams(grinderSettings.targetGrams));
}

static inline void grinderLoadSettings() {
  Preferences preferences;
  if (!preferences.begin("grinder", true)) {
    grinderNormalizeSettings();
    return;
  }
  grinderSettings.enabled = preferences.getBool("enabled", false);
  grinderCopyString(grinderSettings.selectedMac, sizeof(grinderSettings.selectedMac), preferences.getString("mac", ""));
  grinderCopyString(grinderSettings.hostname, sizeof(grinderSettings.hostname), preferences.getString("host", ""));
  grinderSettings.lastIp = IPAddress(preferences.getUInt("ip", 0));
  grinderSettings.targetGrams = preferences.getFloat("target", 15.0f);
  grinderSettings.safetyMarginGrams = preferences.getFloat("safety", 2.0f);
  grinderSettings.zeroMinGrams = preferences.getFloat("zmin", -1.0f);
  grinderSettings.zeroMaxGrams = preferences.getFloat("zmax", 1.0f);
  grinderSettings.targetToleranceGrams = preferences.getFloat("tol", 0.5f);
  grinderSettings.adaptiveSafety.count = (uint8_t)preferences.getUInt("safe_count", 0);
  grinderSettings.adaptiveSafety.next = (uint8_t)preferences.getUInt("safe_next", 0);
  grinderSettings.adaptiveSafety.history[0] = preferences.isKey("safe0") ? preferences.getFloat("safe0", 0.0f) : 0.0f;
  grinderSettings.adaptiveSafety.history[1] = preferences.isKey("safe1") ? preferences.getFloat("safe1", 0.0f) : 0.0f;
  grinderSettings.adaptiveSafety.history[2] = preferences.isKey("safe2") ? preferences.getFloat("safe2", 0.0f) : 0.0f;
  grinderSettings.zeroHoldMs = preferences.getUInt("zerohold", 1000);
  grinderSettings.previousWifiOnBoot = preferences.getBool("wifi_prev", false);
  grinderSettings.previousWifiOnBootSaved = preferences.getBool("wifi_saved", false);
  preferences.end();
  grinderNormalizeSettings();
}

static inline void grinderSaveSettings() {
  grinderNormalizeSettings();
  Preferences preferences;
  if (!preferences.begin("grinder", false)) {
    return;
  }
  preferences.putBool("enabled", grinderSettings.enabled);
  preferences.putString("mac", grinderSettings.selectedMac);
  preferences.putString("host", grinderSettings.hostname);
  preferences.putUInt("ip", (uint32_t)grinderSettings.lastIp);
  preferences.putFloat("target", grinderSettings.targetGrams);
  preferences.putFloat("safety", grinderSettings.safetyMarginGrams);
  preferences.putFloat("zmin", grinderSettings.zeroMinGrams);
  preferences.putFloat("zmax", grinderSettings.zeroMaxGrams);
  preferences.putFloat("tol", grinderSettings.targetToleranceGrams);
  preferences.putUInt("safe_count", grinderSettings.adaptiveSafety.count);
  preferences.putUInt("safe_next", grinderSettings.adaptiveSafety.next);
  preferences.putFloat("safe0", grinderSettings.adaptiveSafety.history[0]);
  preferences.putFloat("safe1", grinderSettings.adaptiveSafety.history[1]);
  preferences.putFloat("safe2", grinderSettings.adaptiveSafety.history[2]);
  preferences.putUInt("zerohold", grinderSettings.zeroHoldMs);
  preferences.putBool("wifi_prev", grinderSettings.previousWifiOnBoot);
  preferences.putBool("wifi_saved", grinderSettings.previousWifiOnBootSaved);
  preferences.end();
  grinderRuntime.settingsDirty = false;
}

#include "grinder_settings_persistence.h"
#include "grinder_runtime_adaptive.h"

static inline const char *grinderStateText(GrinderDosingState state) {
  switch (state) {
    case GRINDER_STATE_DISABLED: return "disabled";
    case GRINDER_STATE_FINDING_PLUG: return "finding_plug";
    case GRINDER_STATE_CONNECTED: return "connected";
    case GRINDER_STATE_ARMED: return "armed";
    case GRINDER_STATE_GRINDING: return "grinding";
    case GRINDER_STATE_STOPPING: return "stopping";
    case GRINDER_STATE_AWAIT_REMOVAL: return "await_removal";
    case GRINDER_STATE_AWAIT_ZERO: return "await_zero";
    case GRINDER_STATE_ERROR: return "error";
    default: return "error";
  }
}

static inline void grinderSetStatus(const char *status) {
  grinderCopyCString(grinderRuntime.status, sizeof(grinderRuntime.status), status);
}

static inline void grinderSetState(GrinderDosingState state) {
  if (grinderRuntime.state != state) {
    Serial.printf("[grinder] state %s -> %s status=%s\n",
                  grinderStateText(grinderRuntime.state),
                  grinderStateText(state),
                  grinderRuntime.status);
    grinderRuntime.state = state;
    grinderRuntime.stateEnteredAt = millis();
    grinderRuntime.zeroStableSince = 0;
    grinderRuntime.zeroTracking = false;
  }
}

static inline bool grinderRuntimeLogDue(uint32_t intervalMs) {
  const uint32_t now = millis();
  if (now - grinderRuntime.lastRuntimeLogAt < intervalMs) {
    return false;
  }
  grinderRuntime.lastRuntimeLogAt = now;
  return true;
}

static inline void grinderFormatScaleMac() {
  uint8_t mac[6] = { 0, 0, 0, 0, 0, 0 };
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    WiFi.macAddress(mac);
  }
  snprintf(grinderRuntime.scaleMac, sizeof(grinderRuntime.scaleMac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[grinder] scale mac=%s\n", grinderRuntime.scaleMac);
}

static inline void grinderResetLineReader() {
  grinderRuntime.rxLength = 0;
  grinderRuntime.rxLine[0] = 0;
  grinderRuntime.pendingCr = false;
}

static inline void grinderCloseClient() {
  grinderRuntime.client.stop();
  grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
  grinderRuntime.relayOn = false;
  grinderRuntime.activePlugMac[0] = 0;
  grinderResetLineReader();
}

static inline void grinderResetCutoffGuard();
static inline void grinderResetGrindConfirmation();

static inline void grinderDisconnectToFinding() {
  grinderCloseClient();
  grinderRuntime.resolvePhase = 0;
  grinderRuntime.lastConnectAttempt = 0;
  grinderRuntime.grindRateGps = 0.0f;
  grinderRuntime.rateSamples = 0;
  grinderResetCutoffGuard();
  grinderResetGrindConfirmation();
  grinderRuntime.tarePending = false;
  grinderRuntime.tareRequestArmsGrinder = false;
  grinderRuntime.tareRearmRequested = false;
  grinderRuntime.userTareComplete = false;
  grinderAdaptiveShotReset(&grinderRuntime.adaptiveShot);
  grinderSetState(grinderSettings.enabled ? GRINDER_STATE_FINDING_PLUG : GRINDER_STATE_DISABLED);
}

static inline bool grinderSendOff();

static inline void grinderEnterError(const char *status) {
  if (grinderRuntime.client.connected()) {
    grinderSendOff();
  }
  Serial.printf("[grinder] error %s\n", status);
  grinderSetStatus(status);
  grinderCloseClient();
  grinderSetState(GRINDER_STATE_ERROR);
}

static inline bool grinderSelectedMacSet() {
  return grinderIsMac(grinderSettings.selectedMac);
}

static inline bool grinderResponseMatchesSelection(const GrinderTcpResponse &response) {
  return grinderSelectedMacSet() && strcmp(response.plugMac, grinderSettings.selectedMac) == 0;
}

static inline bool grinderSendLine(const char *line, GrinderPendingCommand command) {
  if (!grinderRuntime.client.connected()) {
    return false;
  }
  grinderRuntime.client.print(line);
  grinderRuntime.client.print('\n');
  grinderRuntime.pendingCommand = command;
  grinderRuntime.lastCommandAt = millis();
  if (command == GRINDER_COMMAND_PING) {
    grinderRuntime.lastPingAt = grinderRuntime.lastCommandAt;
  } else {
    Serial.printf("[grinder] tx %s state=%s\n", line, grinderStateText(grinderRuntime.state));
  }
  return true;
}

static inline bool grinderSendHello() {
  char command[32];
  grinderFormatHello(command, sizeof(command), grinderRuntime.scaleMac);
  return grinderSendLine(command, GRINDER_COMMAND_HELLO);
}

static inline bool grinderSendSimpleCommand(const char *line, GrinderPendingCommand command) {
  return grinderSendLine(line, command);
}

static inline void grinderClearDosingMetrics(float weight) {
  grinderRuntime.grindRateGps = 0.0f;
  grinderRuntime.rateSamples = 0;
  grinderRuntime.lastWeight = weight;
  grinderRuntime.lastWeightAt = millis();
  grinderRuntime.stopWeight = 0.0f;
  grinderRuntime.removalSeen = false;
  grinderResetCutoffGuard();
  grinderResetGrindConfirmation();
  grinderAdaptiveShotReset(&grinderRuntime.adaptiveShot);
}

static inline bool grinderWeightInZeroRange(float weight) {
  return weight >= grinderSettings.zeroMinGrams && weight <= grinderSettings.zeroMaxGrams;
}

static inline bool grinderWeightInPositiveDoseRange(float weight) {
  return weight > grinderSettings.zeroMaxGrams;
}

static inline bool grinderZeroStable(float weight) {
  const uint32_t now = millis();
  if (!grinderWeightInZeroRange(weight)) {
    grinderRuntime.zeroStableSince = 0;
    grinderRuntime.zeroTracking = false;
    return false;
  }
  if (!grinderRuntime.zeroTracking || fabsf(weight - grinderRuntime.zeroTrackWeight) > 0.15f) {
    grinderRuntime.zeroTracking = true;
    grinderRuntime.zeroTrackWeight = weight;
    grinderRuntime.zeroStableSince = now;
    return false;
  }
  return now - grinderRuntime.zeroStableSince >= grinderSettings.zeroHoldMs;
}

static inline void grinderUpdateRate(float weight) {
  const uint32_t now = millis();
  if (grinderRuntime.lastWeightAt != 0) {
    const float elapsed = (now - grinderRuntime.lastWeightAt) / 1000.0f;
    const float delta = weight - grinderRuntime.lastWeight;
    if (elapsed > 0.02f && delta > 0.03f) {
      const float instantRate = delta / elapsed;
      if (grinderRuntime.rateSamples == 0) {
        grinderRuntime.grindRateGps = instantRate;
      } else {
        grinderRuntime.grindRateGps = grinderRuntime.grindRateGps * 0.7f + instantRate * 0.3f;
      }
      if (grinderRuntime.rateSamples < 255) {
        grinderRuntime.rateSamples++;
      }
    }
  }
  grinderRuntime.lastWeight = weight;
  grinderRuntime.lastWeightAt = now;
}

static inline bool grinderPlugConnectionStale(uint32_t now) {
  if (!grinderRuntime.client.connected()) {
    return true;
  }
  return grinderHeartbeatLost(grinderRuntime.missedPingResponses, grinderRuntime.lastRxAt, now);
}

static inline void grinderCheckConnectionLoss();

#include "grinder_runtime_low_latency.h"

static inline bool grinderLineRead(char value) {
  if (value == '\r') {
    if (grinderRuntime.pendingCr) {
      grinderResetLineReader();
    } else {
      grinderRuntime.pendingCr = true;
    }
    return false;
  }
  if (value == '\n') {
    grinderRuntime.pendingCr = false;
    grinderRuntime.rxLine[grinderRuntime.rxLength] = 0;
    return true;
  }
  if (grinderRuntime.pendingCr || value < 32 || value > 126 || grinderRuntime.rxLength >= GRINDER_TCP_MAX_LINE_LENGTH) {
    grinderResetLineReader();
    return false;
  }
  grinderRuntime.rxLine[grinderRuntime.rxLength++] = value;
  grinderRuntime.rxLine[grinderRuntime.rxLength] = 0;
  return false;
}

static inline void grinderHandleOkResponse(const GrinderTcpResponse &response, float weight) {
  grinderRuntime.relayOn = response.relayOn;
  grinderCopyCString(grinderRuntime.activePlugMac, sizeof(grinderRuntime.activePlugMac), response.plugMac);
  if (grinderRuntime.pendingCommand != GRINDER_COMMAND_PING) {
    Serial.printf("[grinder] rx OK %s state=%s pending=%d weight=%.2f\n",
                  response.relayOn ? "ON" : "OFF",
                  grinderStateText(grinderRuntime.state),
                  (int)grinderRuntime.pendingCommand,
                  weight);
  }
  switch (grinderRuntime.pendingCommand) {
    case GRINDER_COMMAND_HELLO:
      grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
      grinderRuntime.tareRequestArmsGrinder = false;
      grinderRuntime.tareRearmRequested = response.relayOn;
      grinderSetStatus(grinderRuntime.userTareComplete ? "zero wait" : "tare to arm");
      if (response.relayOn) {
        if (!grinderSendOff()) {
          grinderEnterError("off send failed");
          break;
        }
        grinderSetState(GRINDER_STATE_STOPPING);
      } else {
        grinderSetState(GRINDER_STATE_CONNECTED);
      }
      break;
    case GRINDER_COMMAND_ON:
      grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
      if (response.relayOn) {
        grinderClearDosingMetrics(weight);
        grinderSetStatus("ready");
        grinderSetState(GRINDER_STATE_GRINDING);
      } else {
        grinderEnterError("on failed");
      }
      break;
    case GRINDER_COMMAND_OFF:
      if (!response.relayOn) {
        grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
        grinderMarkAdaptiveShotOff(weight);
        grinderRuntime.stopWeight = weight;
        grinderRuntime.removalSeen = false;
        if (grinderRuntime.tareRearmRequested) {
          grinderRuntime.tareRearmRequested = false;
          grinderSetStatus("zero wait");
          grinderSetState(GRINDER_STATE_CONNECTED);
        } else {
          grinderSetStatus("remove cup");
          grinderSetState(GRINDER_STATE_AWAIT_REMOVAL);
        }
      }
      break;
    case GRINDER_COMMAND_PING:
      grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
      if (grinderRuntime.state == GRINDER_STATE_GRINDING && !response.relayOn) {
        grinderEnterError("relay off");
      }
      break;
    case GRINDER_COMMAND_BYE:
      grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
      grinderDisconnectToFinding();
      break;
    default:
      break;
  }
}

static inline void grinderHandleResponseLine(float weight) {
  GrinderTcpResponse response;
  if (!grinderParseResponse(grinderRuntime.rxLine, &response)) {
    Serial.printf("[grinder] bad response line=%s\n", grinderRuntime.rxLine);
    grinderEnterError("bad response");
    return;
  }
  if (!grinderResponseMatchesSelection(response)) {
    grinderEnterError("wrong mac");
    return;
  }
  grinderRuntime.lastRxAt = millis();
  grinderRuntime.missedPingResponses = 0;
  if (response.kind == GRINDER_TCP_RESPONSE_BUSY) {
    grinderEnterError("busy");
    return;
  }
  if (response.kind == GRINDER_TCP_RESPONSE_ERR) {
    grinderEnterError(response.reason);
    return;
  }
  if (response.kind == GRINDER_TCP_RESPONSE_OK) {
    grinderHandleOkResponse(response, weight);
  }
}

static inline void grinderReadClient(float weight) {
  while (grinderRuntime.client.connected() && grinderRuntime.client.available()) {
    const int value = grinderRuntime.client.read();
    if (value < 0) {
      return;
    }
    if (grinderLineRead((char)value)) {
      grinderHandleResponseLine(weight);
      grinderResetLineReader();
      if (grinderRuntime.state == GRINDER_STATE_ERROR) {
        return;
      }
    }
  }
}

#include "grinder_discovery.h"

static inline bool grinderAttemptConnect(IPAddress ip) {
  if (!grinderIpValid(ip)) {
    grinderSetStatus("plug wait");
    return false;
  }
  grinderCloseClient();
  grinderSetStatus("connecting");
  grinderRuntime.client.setTimeout(50);
  if (!grinderRuntime.client.connect(ip, GRINDER_TCP_PORT, GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS)) {
    grinderSetStatus("plug wait");
    if (grinderRuntimeLogDue(10000)) {
      Serial.printf("[grinder] plug wait ip=%s\n", ip.toString().c_str());
    }
    grinderCloseClient();
    return false;
  }
  Serial.printf("[grinder] connect ip=%s selected=%s\n",
                ip.toString().c_str(),
                grinderSettings.selectedMac);
  grinderRuntime.client.setNoDelay(true);
  grinderRuntime.lastRxAt = millis();
  grinderRuntime.lastPingAt = 0;
  grinderRuntime.missedPingResponses = 0;
  grinderResetLineReader();
  if (!grinderSendHello()) {
    Serial.println("[grinder] HELLO send failed");
    grinderCloseClient();
    return false;
  }
  return true;
}

static inline bool grinderAttemptHostnameConnect() {
  if (grinderSettings.hostname[0] == 0) {
    grinderSetStatus("plug wait");
    return false;
  }
  char host[64];
  grinderCopyCString(host, sizeof(host), grinderSettings.hostname);
  const size_t len = strlen(host);
  if (len > 6 && strcmp(host + len - 6, ".local") == 0) {
    host[len - 6] = 0;
  }
  IPAddress ip = MDNS.queryHost(host, GRINDER_RUNTIME_HOST_RESOLVE_TIMEOUT_MS);
  if (!grinderIpValid(ip)) {
    grinderSetStatus("plug wait");
    if (grinderRuntimeLogDue(10000)) {
      Serial.printf("[grinder] plug wait host=%s\n", host);
    }
    return false;
  }
  Serial.printf("[grinder] host resolved host=%s ip=%s\n", host, ip.toString().c_str());
  grinderSaveLastIpIfChanged(ip);
  return grinderAttemptConnect(ip);
}

static inline void grinderResolveAndConnect() {
  const uint32_t now = millis();
  if (grinderRuntime.pendingCommand == GRINDER_COMMAND_HELLO) {
    if (now - grinderRuntime.lastCommandAt > 1200) {
      grinderCloseClient();
      grinderRuntime.resolvePhase = (grinderRuntime.resolvePhase + 1) % 2;
    }
    return;
  }
  if (now - grinderRuntime.lastConnectAttempt < GRINDER_RUNTIME_RECONNECT_INTERVAL_MS) {
    return;
  }
  grinderRuntime.lastConnectAttempt = now;
  if (grinderRuntime.resolvePhase == 0) {
    grinderRuntime.resolvePhase = 1;
    grinderAttemptConnect(grinderSettings.lastIp);
    return;
  }
  grinderRuntime.resolvePhase = 0;
  grinderAttemptHostnameConnect();
}

static inline void grinderSendPingIfDue() {
  if (grinderRuntime.pendingCommand != GRINDER_COMMAND_NONE) {
    return;
  }
  if (millis() - grinderRuntime.lastPingAt >= GRINDER_HEARTBEAT_INTERVAL_MS) {
    grinderSendSimpleCommand("PING", GRINDER_COMMAND_PING);
  }
}

static inline void grinderCheckConnectionLoss() {
  if (grinderRuntime.state == GRINDER_STATE_DISABLED ||
      grinderRuntime.state == GRINDER_STATE_FINDING_PLUG ||
      grinderRuntime.state == GRINDER_STATE_ERROR) {
    return;
  }
  const uint32_t now = millis();
  if (grinderRuntime.pendingCommand == GRINDER_COMMAND_PING &&
      now - grinderRuntime.lastCommandAt >= GRINDER_HEARTBEAT_RESPONSE_TIMEOUT_MS) {
    grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
    if (grinderRuntime.missedPingResponses < UINT8_MAX) {
      grinderRuntime.missedPingResponses++;
    }
    Serial.printf("[grinder] heartbeat miss=%u\n", grinderRuntime.missedPingResponses);
  }
  if (grinderPlugConnectionStale(now)) {
    if (grinderRuntime.state == GRINDER_STATE_GRINDING || grinderRuntime.state == GRINDER_STATE_STOPPING) {
      grinderSendOff();
    }
    grinderSetStatus("lost plug");
    grinderDisconnectToFinding();
  }
}

static inline void grinderTickConnected(float weight) {
  if (!grinderRuntime.userTareComplete) {
    grinderSetStatus("tare to arm");
    return;
  }
  if (grinderCanArmAfterTare(grinderRuntime.userTareComplete, grinderZeroStable(weight))) {
    grinderSetStatus("armed");
    grinderSetState(GRINDER_STATE_ARMED);
  } else if (millis() - grinderRuntime.lastRuntimeLogAt >= 2000) {
    grinderRuntime.lastRuntimeLogAt = millis();
    Serial.printf("[grinder] wait zero weight=%.2f range=%.1f..%.1f\n",
                  weight,
                  grinderSettings.zeroMinGrams,
                  grinderSettings.zeroMaxGrams);
  }
}

static inline void grinderTickArmed() {
  const uint32_t now = millis();
  if (grinderRuntime.pendingCommand == GRINDER_COMMAND_NONE) {
    Serial.println("[grinder] armed send ON");
    grinderSendSimpleCommand("ON", GRINDER_COMMAND_ON);
    return;
  }
  if (grinderRuntime.pendingCommand == GRINDER_COMMAND_ON &&
      now - grinderRuntime.lastCommandAt > 2500) {
    grinderEnterError("on timeout");
  }
}

static inline void grinderTickStopping(float weight) {
  grinderTrackAdaptiveShot(weight);
  const uint32_t now = millis();
  if (now - grinderRuntime.stateEnteredAt > 2500) {
    grinderEnterError("off timeout");
    return;
  }
  if (grinderRuntime.pendingCommand != GRINDER_COMMAND_OFF) {
    grinderSendOff();
    return;
  }
  if (now - grinderRuntime.lastCommandAt >= 150) {
    grinderSendOff();
  }
}

static inline bool grinderWeightIndicatesRemoval(float weight) {
  const float negativeRemoval = grinderSettings.zeroMinGrams - grinderSettings.targetToleranceGrams;
  if (weight <= negativeRemoval) {
    return true;
  }
  if (!grinderWeightInZeroRange(weight)) {
    return false;
  }
  const float observedDoseWeight = grinderRuntime.stopWeight > grinderRuntime.adaptiveShot.finalWeight ? grinderRuntime.stopWeight : grinderRuntime.adaptiveShot.finalWeight;
  return observedDoseWeight > grinderSettings.zeroMaxGrams + grinderSettings.targetToleranceGrams &&
         observedDoseWeight - weight >= GRINDER_ADAPTIVE_MIN_SPAN_GRAMS;
}

static inline void grinderTickAwaitRemoval(float weight) {
  const bool removed = grinderWeightIndicatesRemoval(weight);
  if (removed && !grinderRuntime.adaptiveShot.finalWeightLocked) {
    grinderInvalidateAdaptiveShot(GRINDER_ADAPTIVE_SKIP_REMOVED);
  } else {
    grinderTrackAdaptiveShot(weight);
  }
  grinderLearnAdaptiveSafetyIfReady();
  if (removed) {
    grinderLearnAdaptiveSafety();
    grinderRuntime.removalSeen = true;
    grinderSetStatus("await zero");
    grinderSetState(GRINDER_STATE_AWAIT_ZERO);
  }
}

static inline void grinderTickAwaitZero(float weight) {
  if (grinderRuntime.removalSeen && grinderZeroStable(weight)) {
    grinderSetStatus("armed");
    grinderSetState(GRINDER_STATE_ARMED);
  }
}

static inline void grinderRuntimeBegin() {
  grinderFormatScaleMac();
  grinderSetStatus(grinderSettings.enabled ? "idle" : "off");
  grinderSetState(grinderSettings.enabled ? GRINDER_STATE_FINDING_PLUG : GRINDER_STATE_DISABLED);
}

static inline void grinderRuntimeReset() {
  grinderCloseClient();
  grinderRuntime.resolvePhase = 0;
  grinderRuntime.lastConnectAttempt = 0;
  grinderRuntime.lastPingAt = 0;
  grinderRuntime.lastRxAt = 0;
  grinderRuntime.lastCommandAt = 0;
  grinderRuntime.zeroStableSince = 0;
  grinderRuntime.lastRuntimeLogAt = 0;
  grinderRuntime.grindRateGps = 0.0f;
  grinderRuntime.rateSamples = 0;
  grinderRuntime.removalSeen = false;
  grinderRuntime.lastFastWeightSequence = 0;
  grinderRuntime.missedPingResponses = 0;
  grinderResetCutoffGuard();
  grinderResetGrindConfirmation();
  grinderRuntime.tarePending = false;
  grinderRuntime.tareRequestArmsGrinder = false;
  grinderRuntime.userTareComplete = false;
  grinderRuntime.tareRearmRequested = false;
  grinderAdaptiveShotReset(&grinderRuntime.adaptiveShot);
  grinderSetStatus(grinderSettings.enabled ? "idle" : "off");
  grinderSetState(grinderSettings.enabled ? GRINDER_STATE_FINDING_PLUG : GRINDER_STATE_DISABLED);
}

static inline void grinderPauseForMenu() {
  if (!grinderSettings.enabled) {
    return;
  }
  grinderRuntime.menuPaused = true;
  if (!grinderRuntime.client.connected() ||
      (grinderRuntime.state != GRINDER_STATE_ARMED && grinderRuntime.state != GRINDER_STATE_GRINDING)) {
    return;
  }
  if (!grinderSendOff()) {
    grinderEnterError("off send failed");
    return;
  }
  grinderRuntime.tareRearmRequested = true;
  grinderSetStatus("menu stop");
  grinderSetState(GRINDER_STATE_STOPPING);
}

static inline void grinderResumeAfterMenu() {
  grinderRuntime.menuPaused = false;
}

static inline void grinderTickWhileMenuOpen(float weight) {
  if (!grinderSelectedMacSet() || grinderRuntime.state == GRINDER_STATE_FINDING_PLUG ||
      grinderRuntime.state == GRINDER_STATE_ERROR) {
    return;
  }
  grinderReadClient(weight);
  grinderCheckConnectionLoss();
  if (grinderRuntime.state == GRINDER_STATE_ERROR) {
    return;
  }
  if (grinderRuntime.state == GRINDER_STATE_STOPPING) {
    grinderTickStopping(weight);
    return;
  }
  grinderSendPingIfDue();
}

static inline void grinderRuntimeTick(float weight) {
  if (!grinderSettings.enabled) {
    if (grinderRuntime.state != GRINDER_STATE_DISABLED) {
      grinderRuntimeReset();
    }
    return;
  }
  if (grinderRuntime.menuPaused) {
    grinderTickWhileMenuOpen(weight);
    return;
  }
  if (!grinderSelectedMacSet()) {
    grinderSetStatus("plug wait");
    if (grinderRuntime.state != GRINDER_STATE_FINDING_PLUG) {
      grinderCloseClient();
      grinderSetState(GRINDER_STATE_FINDING_PLUG);
    }
    return;
  }
  grinderReadClient(weight);
  grinderCheckConnectionLoss();
  if (grinderRuntime.state == GRINDER_STATE_ERROR) {
    return;
  }
  switch (grinderRuntime.state) {
    case GRINDER_STATE_FINDING_PLUG:
      if (WiFi.status() == WL_CONNECTED) {
        grinderResolveAndConnect();
      } else {
        grinderSetStatus("wifi wait");
      }
      break;
    case GRINDER_STATE_CONNECTED:
      grinderTickConnected(weight);
      grinderSendPingIfDue();
      break;
    case GRINDER_STATE_ARMED:
      grinderTickArmed();
      break;
    case GRINDER_STATE_GRINDING:
      grinderTrackAdaptiveShot(weight);
      grinderSendPingIfDue();
      break;
    case GRINDER_STATE_STOPPING:
      grinderTickStopping(weight);
      break;
    case GRINDER_STATE_AWAIT_REMOVAL:
      grinderTickAwaitRemoval(weight);
      grinderSendPingIfDue();
      break;
    case GRINDER_STATE_AWAIT_ZERO:
      grinderTickAwaitZero(weight);
      grinderSendPingIfDue();
      break;
    default:
      break;
  }
}

static inline bool grinderRuntimeKeepsAwake() {
  return grinderSettings.enabled && grinderRuntime.state != GRINDER_STATE_DISABLED;
}

static inline void grinderReleaseClientForDiscovery() {
  if (grinderRuntime.client.connected()) {
    grinderSendOff();
    delay(250);
    grinderRuntime.client.print("BYE\n");
    delay(250);
  }
  grinderCloseClient();
  delay(250);
}

static inline void grinderShortStatus(char *output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  if (!grinderSettings.enabled) {
    snprintf(output, outputSize, "Gr:Off");
    return;
  }
  snprintf(output,
           outputSize,
           "Gr:%s",
           grinderRuntime.status[0] ? grinderRuntime.status : grinderStateText(grinderRuntime.state));
}

static inline void grinderSetEnabled(bool enabled) {
  if (!enabled && grinderRuntime.client.connected()) {
    grinderSendOff();
  }
  grinderSettings.enabled = enabled;
  grinderSaveSettings();
  grinderRuntimeReset();
}

#endif
