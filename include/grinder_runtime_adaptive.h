#ifndef GRINDER_RUNTIME_ADAPTIVE_H
#define GRINDER_RUNTIME_ADAPTIVE_H

static inline void grinderResetAdaptiveSafety() {
  grinderAdaptiveSafetyClear(&grinderSettings.adaptiveSafety);
}

static inline void grinderTrackAdaptiveShot(float weight) {
  grinderAdaptiveShotTrack(&grinderRuntime.adaptiveShot,
                           weight,
                           grinderSettings.zeroMaxGrams,
                           grinderSettings.targetGrams,
                           millis());
}

static inline void grinderMarkAdaptiveShotOff(float weight) {
  grinderAdaptiveShotMarkOff(&grinderRuntime.adaptiveShot,
                             weight,
                             grinderSettings.zeroMaxGrams,
                             grinderSettings.targetGrams,
                             millis());
}

static inline void grinderInvalidateAdaptiveShot(uint8_t reason) {
  grinderAdaptiveShotInvalidate(&grinderRuntime.adaptiveShot, reason);
}

static inline void grinderLearnAdaptiveSafety() {
  if (grinderRuntime.adaptiveShot.learned) {
    return;
  }
  grinderRuntime.adaptiveShot.learned = true;
  const float previousSafety = grinderSettings.safetyMarginGrams;
  const float maxSafety = grinderMaxSafetyGrams(grinderSettings.targetGrams);
  const GrinderAdaptiveSafetyResult result = grinderAdaptiveSafetyCalculate(previousSafety,
                                                                           grinderSettings.targetGrams,
                                                                           maxSafety,
                                                                           &grinderRuntime.adaptiveShot);
  if (!result.valid) {
    Serial.printf("[grinder] safety learn skipped reason=%s final=%.2f target=%.2f avg=%.2f\n",
                  grinderAdaptiveSkipReasonText(result.skipReason),
                  grinderRuntime.adaptiveShot.finalWeight,
                  grinderSettings.targetGrams,
                  result.averageRateGps);
    return;
  }
  grinderSettings.safetyMarginGrams = grinderAdaptiveSafetyRecord(&grinderSettings.adaptiveSafety,
                                                                  result.recommendationGrams,
                                                                  previousSafety,
                                                                  maxSafety);
  grinderMarkSettingsDirty();
  Serial.printf("[grinder] safety learn final=%.2f target=%.2f error=%.2f avg=%.2f safety=%.2f->%.2f count=%u hist=%.2f,%.2f,%.2f\n",
                grinderRuntime.adaptiveShot.finalWeight,
                grinderSettings.targetGrams,
                result.errorGrams,
                result.averageRateGps,
                previousSafety,
                grinderSettings.safetyMarginGrams,
                grinderSettings.adaptiveSafety.count,
                grinderSettings.adaptiveSafety.history[0],
                grinderSettings.adaptiveSafety.history[1],
                grinderSettings.adaptiveSafety.history[2]);
}

static inline void grinderLearnAdaptiveSafetyIfReady() {
  if (!grinderRuntime.adaptiveShot.learned && grinderAdaptiveShotReadyToLearn(&grinderRuntime.adaptiveShot)) {
    grinderLearnAdaptiveSafety();
  }
}

#endif
