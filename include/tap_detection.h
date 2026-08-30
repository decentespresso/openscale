#ifndef TAP_DETECTION_H
#define TAP_DETECTION_H

#include <Arduino.h>
#include <math.h>
#include "parameter.h"
#include "finger_detection.h"

// Pan double/triple tap detection on the raw weight signal:
// 1. steady state: sample every 100ms; 5 samples (500ms) spread <0.5g -> baseline
// 2. peak: rise then fall, height above baseline >20g
// 3. sequence: peaks closer than 400ms chain up
//    - 2 peaks, no 3rd within 400ms = double tap -> tare
//    - 3 consecutive peaks = triple tap -> timer (start/stop/reset cycle)
// 4. fire 500ms after the decision (peak sequence rejects placed objects)
constexpr float TAP_PEAK_G = 20.0f;
constexpr float TAP_PEAK_SLOPE = 2.0f;
constexpr unsigned long TAP_SAMPLE_MS = 100;
constexpr uint8_t TAP_STEADY_N = 5;
constexpr float TAP_STEADY_RANGE = 0.5f;
constexpr unsigned long TAP_DOUBLE_MS = 400;
constexpr unsigned long TAP_TARE_DELAY_MS = 500;

static float tapHist[TAP_STEADY_N] = {0};
static uint8_t tapHistIdx = 0;
static uint8_t tapHistCount = 0;
static unsigned long tapSampleMs = 0;
static bool tapSteady = false;
static float tapBaseline = 0.0f;
static float tapPrevW = 0.0f;
static bool tapRising = false;
static unsigned long tapLastPeakMs = 0;
static uint8_t tapSeqCount = 0;
static bool tapDecisionPending = false;
static unsigned long tapDecisionAtMs = 0;
static bool tapActionArmed = false;
static unsigned long tapActionAtMs = 0;
static bool tapTripleArmed = false;

void tapDetectTick() {
  if (b_bootTare || b_bootFreshTarePending) {
    return;
  }
  unsigned long now = millis();
  float w = f_current_raw_value;

  if (now - tapSampleMs >= TAP_SAMPLE_MS) {
    tapSampleMs = now;
    tapHist[tapHistIdx] = w;
    tapHistIdx = (tapHistIdx + 1) % TAP_STEADY_N;
    if (tapHistCount < TAP_STEADY_N) tapHistCount++;
    if (tapHistCount >= TAP_STEADY_N) {
      float lo = tapHist[0];
      float hi = tapHist[0];
      float sum = 0;
      for (uint8_t i = 0; i < TAP_STEADY_N; i++) {
        if (tapHist[i] < lo) lo = tapHist[i];
        if (tapHist[i] > hi) hi = tapHist[i];
        sum += tapHist[i];
      }
      if (hi - lo < TAP_STEADY_RANGE) {
        tapSteady = true;
        tapBaseline = sum / TAP_STEADY_N;
      } else if (fabsf(w - tapBaseline) > TAP_PEAK_SLOPE) {
        tapSteady = false;
      }
    }
  }

  if (w > tapPrevW + TAP_PEAK_SLOPE) {
    tapRising = true;
  } else if (w < tapPrevW - TAP_PEAK_SLOPE && tapRising) {
    tapRising = false;
    float height = tapPrevW - tapBaseline;
    if (height > TAP_PEAK_G) {
      if (now - tapLastPeakMs < TAP_DOUBLE_MS) {
        tapSeqCount++;
        if (tapSeqCount == 3) {
          tapDecisionPending = false;
          tapSeqCount = 0;
          if (b_tapTimerEnabled) {
            tapActionArmed = true;
            tapTripleArmed = true;
            tapActionAtMs = now;
            Serial.println("[TAP] triple tap -> timer");
          }
        } else {
          tapDecisionPending = true;
          tapDecisionAtMs = now;
        }
      } else {
        tapSeqCount = 1;
      }
      tapLastPeakMs = now;
    }
  }
  tapPrevW = w;

  if (tapDecisionPending && now - tapDecisionAtMs >= TAP_DOUBLE_MS) {
    tapDecisionPending = false;
    tapSeqCount = 0;
    if (b_tapTareEnabled) {
      tapActionArmed = true;
      tapTripleArmed = false;
      tapActionAtMs = now;
      Serial.println("[TAP] double tap -> tare");
    }
  }

  if (tapActionArmed && now - tapActionAtMs >= TAP_TARE_DELAY_MS) {
    tapActionArmed = false;
    if (millis() - t_menuExitTime > 1000) {
      if (tapTripleArmed) {
        scaleTimer();
      } else {
        b_weight_quick_zero = true;
        t_quickZeroStart = millis();
        t_tareByButton = millis();
        b_tareByButton = true;
      }
#ifdef BUZZER
      buzzer.beep(1, BUZZER_DURATION);
#endif
    }
  }
}

#endif
