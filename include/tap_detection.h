#ifndef TAP_DETECTION_H
#define TAP_DETECTION_H

#include <Arduino.h>
#include "finger_detection.h"
#include "parameter.h"
#include "tap_detector.h"

constexpr unsigned long TAP_ACTION_DELAY_MS = 500;

static TapDetector tapDetector;
static bool tapActionArmed = false;
static bool tapTripleArmed = false;
static unsigned long tapActionAtMs = 0;
static bool tapDetectionGated = true;

void tapDetectTick() {
  const unsigned long now = millis();
  const float weight = f_current_raw_value;
  if ((!b_tapTareEnabled && !b_tapTimerEnabled) || b_bootTare ||
      b_bootFreshTarePending || now - t_menuExitTime <= 1000) {
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
  if ((event == TapEvent::Double && b_tapTareEnabled) ||
      (event == TapEvent::Triple && b_tapTimerEnabled)) {
    tapActionArmed = true;
    tapTripleArmed = event == TapEvent::Triple;
    tapActionAtMs = now;
    Serial.println(tapTripleArmed ? "[TAP] triple tap -> timer" :
                                   "[TAP] double tap -> tare");
  }

  if (tapActionArmed && now - tapActionAtMs >= TAP_ACTION_DELAY_MS) {
    tapActionArmed = false;
    runRecognizedButtonAction(tapTripleArmed ? BUTTON_SQUARE : BUTTON_CIRCLE);
#ifdef BUZZER
    buzzer.beep(1, BUZZER_DURATION);
#endif
  }
}

#endif
