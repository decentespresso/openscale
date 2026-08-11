#ifndef PRESSENSOR_RUNTIME_H
#define PRESSENSOR_RUNTIME_H

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "pressensor_ble.h"

#define PRESSENSOR_FLOW_SAMPLE_MS 100
#define PRESSENSOR_FLOW_EMA_ALPHA 0.3f
#define PRESSENSOR_MAX_FLOW_GPS 8.0f
#define PRESSENSOR_FLOW_STOP_EPS 0.35f
#define PRESSENSOR_FLOW_STOP_MS 3000
#define PRESSENSOR_MIN_STOP_WEIGHT 8.0f
#define PRESSENSOR_START_BAR_DEFAULT 1.0f
#define PRESSENSOR_REARM_RATIO 0.6f
#define PRESSENSOR_FLUSH_MAX_MS 15000
#define PRESSENSOR_FLUSH_GRACE_MS 3000
#define PRESSENSOR_BACKSTOP_MS 90000
#define PRESSENSOR_BACKSTOP_MIN_WEIGHT 1.0f

enum PressensorShotState {
  PRESSENSOR_SHOT_IDLE,
  PRESSENSOR_SHOT_RUNNING,
  PRESSENSOR_SHOT_DONE
};

struct PressensorSettings {
  bool enabled = false;
  char selectedMac[18] = { 0 };
  char selectedName[24] = { 0 };
  bool autoStart = true;
  bool autoStop = true;
  float startBar = PRESSENSOR_START_BAR_DEFAULT;
};

struct PressensorShot {
  PressensorShotState state = PRESSENSOR_SHOT_IDLE;
  bool startedByPressure = false;
  bool hasFlowed = false;
  bool pressureReady = true;
  uint32_t startMs = 0;
  uint32_t lastFlowMs = 0;
  uint32_t shownEndMs = 0;
  uint32_t flushGraceFrom = 0;
  uint32_t lastSampleMs = 0;
  float weightAtStart = 0.0f;
  float lastSampleWeight = 0.0f;
  float flowEma = 0.0f;
  float peakBar = 0.0f;
  float liveBar = 0.0f;
};

PressensorSettings pressensorSettings;
PressensorShot pressensorShot;

static inline float pressensorFlushPeakLimit() {
  const float limit = pressensorSettings.startBar + 1.0f;
  return limit > 2.0f ? limit : 2.0f;
}

static inline void pressensorNormalizeSettings() {
  if (!isfinite(pressensorSettings.startBar) || pressensorSettings.startBar < 0.1f || pressensorSettings.startBar > 10.0f) {
    pressensorSettings.startBar = PRESSENSOR_START_BAR_DEFAULT;
  }
}

static inline void pressensorLoadSettings() {
  Preferences preferences;
  if (!preferences.begin("pressensor", true)) {
    pressensorNormalizeSettings();
    return;
  }
  pressensorSettings.enabled = preferences.getBool("enabled", false);
  preferences.getString("mac", pressensorSettings.selectedMac, sizeof(pressensorSettings.selectedMac));
  preferences.getString("name", pressensorSettings.selectedName, sizeof(pressensorSettings.selectedName));
  pressensorSettings.autoStart = preferences.getBool("astart", true);
  pressensorSettings.autoStop = preferences.getBool("astop", true);
  pressensorSettings.startBar = preferences.getFloat("startbar", PRESSENSOR_START_BAR_DEFAULT);
  preferences.end();
  pressensorNormalizeSettings();
}

static inline void pressensorSaveSettings() {
  pressensorNormalizeSettings();
  Preferences preferences;
  if (!preferences.begin("pressensor", false)) {
    return;
  }
  preferences.putBool("enabled", pressensorSettings.enabled);
  preferences.putString("mac", pressensorSettings.selectedMac);
  preferences.putString("name", pressensorSettings.selectedName);
  preferences.putBool("astart", pressensorSettings.autoStart);
  preferences.putBool("astop", pressensorSettings.autoStop);
  preferences.putFloat("startbar", pressensorSettings.startBar);
  preferences.end();
}

static inline bool pressensorActive() {
  return pressensorSettings.enabled;
}

static inline void pressensorResetShot() {
  pressensorShot.state = PRESSENSOR_SHOT_IDLE;
  pressensorShot.startedByPressure = false;
  pressensorShot.hasFlowed = false;
  pressensorShot.startMs = 0;
  pressensorShot.lastFlowMs = 0;
  pressensorShot.shownEndMs = 0;
  pressensorShot.flushGraceFrom = 0;
  pressensorShot.weightAtStart = 0.0f;
  pressensorShot.flowEma = 0.0f;
  pressensorShot.peakBar = 0.0f;
}

static inline void pressensorApplyLinkTarget() {
  if (pressensorSettings.enabled) {
    pressensorLinkBegin(pressensorSettings.selectedMac);
  } else {
    pressensorLinkStop();
  }
}

static inline void pressensorSetEnabled(bool enabled) {
  pressensorSettings.enabled = enabled;
  pressensorSaveSettings();
  pressensorResetShot();
  pressensorShot.pressureReady = true;
  pressensorApplyLinkTarget();
}

static inline uint32_t pressensorShotElapsedMs() {
  if (pressensorShot.state == PRESSENSOR_SHOT_RUNNING) {
    return millis() - pressensorShot.startMs;
  }
  if (pressensorShot.state == PRESSENSOR_SHOT_DONE) {
    return pressensorShot.shownEndMs;
  }
  return 0;
}

static inline void pressensorStartShot(bool byPressure, float weight) {
  pressensorShot.state = PRESSENSOR_SHOT_RUNNING;
  pressensorShot.startedByPressure = byPressure;
  pressensorShot.hasFlowed = false;
  pressensorShot.startMs = millis();
  pressensorShot.lastFlowMs = pressensorShot.startMs;
  pressensorShot.shownEndMs = 0;
  pressensorShot.flushGraceFrom = 0;
  pressensorShot.weightAtStart = weight;
  pressensorShot.peakBar = 0.0f;
  Serial.printf("[pressensor] shot start %s\n", byPressure ? "pressure" : "manual");
}

static inline void pressensorStopShot(uint32_t endElapsedMs) {
  pressensorShot.state = PRESSENSOR_SHOT_DONE;
  pressensorShot.shownEndMs = endElapsedMs;
  pressensorShot.flushGraceFrom = 0;
  Serial.printf("[pressensor] shot stop %lus peak=%.1f\n", (unsigned long)(endElapsedMs / 1000), pressensorShot.peakBar);
}

static inline void pressensorCancelShot(const char *reason) {
  Serial.printf("[pressensor] shot cancel %s\n", reason);
  pressensorResetShot();
}

static inline void pressensorTimerButton(float weight) {
  if (pressensorShot.state == PRESSENSOR_SHOT_IDLE) {
    pressensorShot.pressureReady = false;
    pressensorStartShot(false, weight);
  } else if (pressensorShot.state == PRESSENSOR_SHOT_RUNNING) {
    pressensorStopShot(millis() - pressensorShot.startMs);
  } else {
    pressensorResetShot();
  }
}

static inline float pressensorDrinkWeight(float weight) {
  const float drink = weight - pressensorShot.weightAtStart;
  return drink > 0.0f ? drink : 0.0f;
}

static inline void pressensorUpdateFlow(float weight, uint32_t now) {
  if (pressensorShot.lastSampleMs == 0) {
    pressensorShot.lastSampleMs = now;
    pressensorShot.lastSampleWeight = weight;
    return;
  }
  const uint32_t deltaMs = now - pressensorShot.lastSampleMs;
  if (deltaMs < PRESSENSOR_FLOW_SAMPLE_MS) {
    return;
  }
  const float instant = (weight - pressensorShot.lastSampleWeight) / (deltaMs / 1000.0f);
  pressensorShot.flowEma += (instant - pressensorShot.flowEma) * PRESSENSOR_FLOW_EMA_ALPHA;
  if (pressensorShot.flowEma < 0.0f) {
    pressensorShot.flowEma = 0.0f;
  } else if (pressensorShot.flowEma > PRESSENSOR_MAX_FLOW_GPS) {
    pressensorShot.flowEma = PRESSENSOR_MAX_FLOW_GPS;
  }
  pressensorShot.lastSampleMs = now;
  pressensorShot.lastSampleWeight = weight;
}

static inline void pressensorAutoStartTick(float bar, float weight) {
  if (bar < pressensorSettings.startBar * PRESSENSOR_REARM_RATIO) {
    pressensorShot.pressureReady = true;
  }
  if (!pressensorSettings.autoStart || !pressensorStreaming()) {
    return;
  }
  if (pressensorShot.state == PRESSENSOR_SHOT_IDLE && pressensorShot.pressureReady && bar >= pressensorSettings.startBar) {
    pressensorShot.pressureReady = false;
    pressensorStartShot(true, weight);
  }
}

static inline void pressensorFlushGuardTick(float bar, float drink, uint32_t now) {
  if (!pressensorShot.startedByPressure) {
    return;
  }
  const uint32_t elapsed = now - pressensorShot.startMs;
  const bool flushProfile = pressensorShot.peakBar < pressensorFlushPeakLimit();
  if (bar >= pressensorSettings.startBar) {
    pressensorShot.flushGraceFrom = 0;
  } else if (elapsed < PRESSENSOR_FLUSH_MAX_MS && flushProfile) {
    if (pressensorShot.flushGraceFrom == 0) {
      pressensorShot.flushGraceFrom = now;
    } else if (now - pressensorShot.flushGraceFrom >= PRESSENSOR_FLUSH_GRACE_MS) {
      pressensorCancelShot("flush");
      return;
    }
  }
  if (elapsed >= PRESSENSOR_BACKSTOP_MS && flushProfile && drink < PRESSENSOR_BACKSTOP_MIN_WEIGHT) {
    pressensorCancelShot("backstop");
  }
}

static inline void pressensorAutoStopTick(float drink, uint32_t now) {
  if (!pressensorSettings.autoStop) {
    return;
  }
  if (pressensorShot.flowEma > PRESSENSOR_FLOW_STOP_EPS) {
    pressensorShot.hasFlowed = true;
    pressensorShot.lastFlowMs = now;
    return;
  }
  if (pressensorShot.hasFlowed && drink >= PRESSENSOR_MIN_STOP_WEIGHT && now - pressensorShot.lastFlowMs >= PRESSENSOR_FLOW_STOP_MS) {
    pressensorStopShot(pressensorShot.lastFlowMs - pressensorShot.startMs);
  }
}

static inline void pressensorRuntimeTick(float weight, bool shotLogicPaused) {
  if (!pressensorSettings.enabled) {
    return;
  }
  if (pressensorGetLinkState() == PRESSENSOR_LINK_OFF && pressensorSettings.selectedMac[0] != 0) {
    pressensorApplyLinkTarget();
  }
  pressensorLinkTick();
  const uint32_t now = millis();
  pressensorUpdateFlow(weight, now);
  const float bar = pressensorStreaming() ? pressensorReadBar() : 0.0f;
  pressensorShot.liveBar = bar > 0.0f ? bar : 0.0f;
  if (shotLogicPaused) {
    return;
  }
  if (pressensorShot.state == PRESSENSOR_SHOT_RUNNING && bar > pressensorShot.peakBar) {
    pressensorShot.peakBar = bar;
  }
  pressensorAutoStartTick(bar, weight);
  if (pressensorShot.state == PRESSENSOR_SHOT_RUNNING) {
    const float drink = pressensorDrinkWeight(weight);
    pressensorFlushGuardTick(bar, drink, now);
    if (pressensorShot.state == PRESSENSOR_SHOT_RUNNING) {
      pressensorAutoStopTick(drink, now);
    }
  }
}

static inline bool pressensorKeepsAwake() {
  return pressensorSettings.enabled && (pressensorShot.state == PRESSENSOR_SHOT_RUNNING || pressensorStreaming());
}

static inline const char *pressensorStatusText() {
  if (!pressensorSettings.enabled) {
    return "off";
  }
  if (pressensorShot.state == PRESSENSOR_SHOT_DONE) {
    return "done";
  }
  if (!pressensorStreaming()) {
    return pressensorSettings.selectedMac[0] == 0 ? "no sensor" : "connecting";
  }
  if (pressensorShot.state == PRESSENSOR_SHOT_IDLE) {
    if (!pressensorSettings.autoStart) {
      return "[] starts";
    }
    return pressensorShot.pressureReady ? "armed" : "rearming";
  }
  return "";
}

#endif
