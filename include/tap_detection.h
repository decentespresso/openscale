#ifndef TAP_DETECTION_H
#define TAP_DETECTION_H

#include <Arduino.h>
#include "finger_detection.h"
#include "parameter.h"
#include "tap_detector.h"

constexpr unsigned long TAP_ACTION_DELAY_MS = 100;

static TapDetector tapDetector;
static bool tapActionArmed = false;
static bool tapTripleArmed = false;
static unsigned long tapActionAtMs = 0;
static bool tapDetectionGated = true;

static inline void runTapLocalAction(bool tripleTap) {
  if (tripleTap) {
    scaleTimer();
    return;
  }

  b_weight_quick_zero = true;
  t_quickZeroStart = millis();
  t_tareByButton = millis();
  b_tareByButton = true;
}

void tapDetectTick() {
  const unsigned long now = millis();
  const float weight = f_current_raw_value;
  const bool timerRunning = stopWatch.isRunning();
  const bool noTapActionAvailable =
      (!b_tapTareEnabled && !b_tapTimerEnabled) ||
      (timerRunning && !b_tapTimerEnabled);

  if (noTapActionAvailable || b_bootTare || b_bootFreshTarePending
#if HDS_ENABLE_GRINDER
      || grinderRuntime.state == GRINDER_STATE_GRINDING
      || grinderRuntime.state == GRINDER_STATE_STOPPING
#endif
      || now - t_menuExitTime <= 1000) {
    if (!tapDetectionGated) {
      tapDetector.reset(now, weight);
      tapActionArmed = false;
      tapDetectionGated = true;
    }
    return;
  }
  if (tapDetectionGated) {
    tapDetector.reset(now, weight);
    tapDetectionGated = false;
  }

  const TapEvent event = tapDetector.tick(now, weight);
  const bool doubleTapAction =
      event == TapEvent::Double && b_tapTareEnabled && !timerRunning;
  const bool tripleTapAction =
      event == TapEvent::Triple && b_tapTimerEnabled;

  if (doubleTapAction || tripleTapAction) {
    tapActionArmed = true;
    tapTripleArmed = tripleTapAction;
    tapActionAtMs = now;
    Serial.println(tapTripleArmed ? "[TAP] triple tap -> timer" :
                                   "[TAP] double tap -> tare");
  }

  if (tapActionArmed && now - tapActionAtMs >= TAP_ACTION_DELAY_MS) {
    tapActionArmed = false;
    power_off(-1);
    runTapLocalAction(tapTripleArmed);
#ifdef BUZZER
    buzzer.beep(1, BUZZER_DURATION);
#endif
  }
}

#endif
