#ifndef GRINDER_ADAPTIVE_SAFETY_H
#define GRINDER_ADAPTIVE_SAFETY_H

#include <Arduino.h>
#include <math.h>

#define GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE 3
#define GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS 6.0f
#define GRINDER_ADAPTIVE_MAX_SINGLE_STEP_GRAMS 2.0f
#define GRINDER_ADAPTIVE_MAX_TARGET_ERROR_GRAMS 5.0f
#define GRINDER_ADAPTIVE_MIN_SPAN_GRAMS 2.0f
#define GRINDER_ADAPTIVE_MIN_DURATION_MS 500
#define GRINDER_ADAPTIVE_FINAL_DELAY_MS 1000
#define GRINDER_ADAPTIVE_FINAL_STABLE_MS 1500
#define GRINDER_ADAPTIVE_FINAL_STABLE_TOLERANCE_GRAMS 0.15f
#define GRINDER_ADAPTIVE_STALL_MS 2000
#define GRINDER_ADAPTIVE_MIN_PROGRESS_GRAMS 0.10f
#define GRINDER_ADAPTIVE_MAX_POST_OFF_DROP_GRAMS 0.50f

enum GrinderAdaptiveSkipReason {
  GRINDER_ADAPTIVE_SKIP_NONE,
  GRINDER_ADAPTIVE_SKIP_INACTIVE,
  GRINDER_ADAPTIVE_SKIP_INVALID,
  GRINDER_ADAPTIVE_SKIP_NOT_FINAL,
  GRINDER_ADAPTIVE_SKIP_DURATION,
  GRINDER_ADAPTIVE_SKIP_SPAN,
  GRINDER_ADAPTIVE_SKIP_RATE,
  GRINDER_ADAPTIVE_SKIP_ERROR,
  GRINDER_ADAPTIVE_SKIP_STALL,
  GRINDER_ADAPTIVE_SKIP_REMOVED,
  GRINDER_ADAPTIVE_SKIP_POST_OFF_DROP
};

struct GrinderAdaptiveSafetyStore {
  float history[GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE] = { 0.0f, 0.0f, 0.0f };
  uint8_t count = 0;
  uint8_t next = 0;
};

struct GrinderAdaptiveShot {
  bool active = false;
  bool learned = false;
  bool valid = true;
  bool finalWeightLocked = false;
  uint8_t skipReason = GRINDER_ADAPTIVE_SKIP_NONE;
  float startWeight = 0.0f;
  float finalWeight = 0.0f;
  float stableWeight = 0.0f;
  uint32_t startAt = 0;
  uint32_t lastIncreaseAt = 0;
  uint32_t offAt = 0;
  uint32_t finalAt = 0;
  uint32_t stableSinceAt = 0;
};

struct GrinderAdaptiveSafetyResult {
  bool valid = false;
  uint8_t skipReason = GRINDER_ADAPTIVE_SKIP_NONE;
  float averageRateGps = 0.0f;
  float errorGrams = 0.0f;
  float recommendationGrams = 0.0f;
};

static inline const char *grinderAdaptiveSkipReasonText(uint8_t reason) {
  switch (reason) {
    case GRINDER_ADAPTIVE_SKIP_NONE: return "none";
    case GRINDER_ADAPTIVE_SKIP_INACTIVE: return "inactive";
    case GRINDER_ADAPTIVE_SKIP_INVALID: return "invalid";
    case GRINDER_ADAPTIVE_SKIP_NOT_FINAL: return "not_final";
    case GRINDER_ADAPTIVE_SKIP_DURATION: return "duration";
    case GRINDER_ADAPTIVE_SKIP_SPAN: return "span";
    case GRINDER_ADAPTIVE_SKIP_RATE: return "rate";
    case GRINDER_ADAPTIVE_SKIP_ERROR: return "error";
    case GRINDER_ADAPTIVE_SKIP_STALL: return "stall";
    case GRINDER_ADAPTIVE_SKIP_REMOVED: return "removed";
    case GRINDER_ADAPTIVE_SKIP_POST_OFF_DROP: return "post_off_drop";
    default: return "unknown";
  }
}

static inline float grinderClampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static inline uint8_t grinderAdaptiveSafetyNormalizeCount(uint8_t count) {
  return count > GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE ? GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE : count;
}

static inline void grinderAdaptiveSafetyNormalize(GrinderAdaptiveSafetyStore *store, float fallback) {
  if (store == nullptr) {
    return;
  }
  store->count = grinderAdaptiveSafetyNormalizeCount(store->count);
  if (store->next >= GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE) {
    store->next = 0;
  }
  const float safeFallback = isfinite(fallback) ? grinderClampFloat(fallback, 0.0f, 10.0f) : 0.2f;
  for (uint8_t index = 0; index < GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE; index++) {
    if (!isfinite(store->history[index]) || store->history[index] < 0.0f || store->history[index] > 10.0f) {
      store->history[index] = safeFallback;
    }
  }
}

static inline void grinderAdaptiveSafetyClear(GrinderAdaptiveSafetyStore *store) {
  if (store == nullptr) {
    return;
  }
  store->count = 0;
  store->next = 0;
  for (uint8_t index = 0; index < GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE; index++) {
    store->history[index] = 0.0f;
  }
}

static inline float grinderAdaptiveSafetyAverage(const GrinderAdaptiveSafetyStore *store, float fallback) {
  if (store == nullptr || store->count == 0) {
    return fallback;
  }
  const uint8_t count = grinderAdaptiveSafetyNormalizeCount(store->count);
  float total = 0.0f;
  for (uint8_t index = 0; index < count; index++) {
    total += store->history[index];
  }
  return total / count;
}

static inline float grinderAdaptiveSafetyRecord(GrinderAdaptiveSafetyStore *store, float recommendation, float fallback) {
  if (store == nullptr || !isfinite(recommendation)) {
    return fallback;
  }
  grinderAdaptiveSafetyNormalize(store, fallback);
  const uint8_t index = store->next % GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE;
  store->history[index] = grinderClampFloat(recommendation, 0.0f, 10.0f);
  if (store->count < GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE) {
    store->count++;
  }
  store->next = (index + 1) % GRINDER_ADAPTIVE_SAFETY_HISTORY_SIZE;
  return grinderAdaptiveSafetyAverage(store, fallback);
}

static inline void grinderAdaptiveShotReset(GrinderAdaptiveShot *shot) {
  if (shot == nullptr) {
    return;
  }
  shot->active = false;
  shot->learned = false;
  shot->valid = true;
  shot->finalWeightLocked = false;
  shot->skipReason = GRINDER_ADAPTIVE_SKIP_NONE;
  shot->startWeight = 0.0f;
  shot->finalWeight = 0.0f;
  shot->stableWeight = 0.0f;
  shot->startAt = 0;
  shot->lastIncreaseAt = 0;
  shot->offAt = 0;
  shot->finalAt = 0;
  shot->stableSinceAt = 0;
}

static inline void grinderAdaptiveShotInvalidate(GrinderAdaptiveShot *shot, uint8_t reason) {
  if (shot == nullptr || shot->learned) {
    return;
  }
  shot->valid = false;
  shot->skipReason = reason;
}

static inline bool grinderAdaptiveShotReadyToLearn(const GrinderAdaptiveShot *shot) {
  return shot != nullptr && shot->active && (shot->finalWeightLocked || !shot->valid);
}

static inline void grinderAdaptiveShotTrack(GrinderAdaptiveShot *shot,
                                            float weight,
                                            float zeroMaxGrams,
                                            float targetGrams,
                                            uint32_t now) {
  if (shot == nullptr || !isfinite(weight)) {
    return;
  }
  if (!shot->active) {
    if (weight > zeroMaxGrams) {
      shot->active = true;
      shot->learned = false;
      shot->valid = true;
      shot->finalWeightLocked = false;
      shot->skipReason = GRINDER_ADAPTIVE_SKIP_NONE;
      shot->startWeight = zeroMaxGrams;
      shot->finalWeight = weight;
      shot->stableWeight = weight;
      shot->startAt = now;
      shot->lastIncreaseAt = now;
      shot->offAt = 0;
      shot->finalAt = 0;
      shot->stableSinceAt = now;
    }
    return;
  }
  if (shot->finalWeightLocked || !shot->valid) {
    return;
  }
  if (shot->offAt != 0) {
    if (weight + GRINDER_ADAPTIVE_MAX_POST_OFF_DROP_GRAMS < shot->finalWeight) {
      grinderAdaptiveShotInvalidate(shot, GRINDER_ADAPTIVE_SKIP_POST_OFF_DROP);
      return;
    }
    if (weight > shot->finalWeight + GRINDER_ADAPTIVE_MIN_PROGRESS_GRAMS) {
      shot->finalWeight = weight;
    }
    if (shot->stableSinceAt == 0 || fabsf(weight - shot->stableWeight) > GRINDER_ADAPTIVE_FINAL_STABLE_TOLERANCE_GRAMS) {
      shot->stableWeight = weight;
      shot->stableSinceAt = now;
      return;
    }
    if (now - shot->offAt >= GRINDER_ADAPTIVE_FINAL_DELAY_MS && now - shot->stableSinceAt >= GRINDER_ADAPTIVE_FINAL_STABLE_MS) {
      shot->finalWeight = weight;
      shot->finalAt = now;
      shot->finalWeightLocked = true;
    }
    return;
  }
  if (weight > shot->finalWeight + GRINDER_ADAPTIVE_MIN_PROGRESS_GRAMS) {
    shot->finalWeight = weight;
    shot->lastIncreaseAt = now;
    return;
  }
  if (shot->offAt == 0 && shot->finalWeight < targetGrams && now - shot->lastIncreaseAt >= GRINDER_ADAPTIVE_STALL_MS) {
    grinderAdaptiveShotInvalidate(shot, GRINDER_ADAPTIVE_SKIP_STALL);
  }
}

static inline void grinderAdaptiveShotMarkOff(GrinderAdaptiveShot *shot,
                                              float weight,
                                              float zeroMaxGrams,
                                              float targetGrams,
                                              uint32_t now) {
  grinderAdaptiveShotTrack(shot, weight, zeroMaxGrams, targetGrams, now);
  if (shot == nullptr || !shot->active || shot->offAt != 0 || !shot->valid) {
    return;
  }
  shot->offAt = now;
  shot->stableWeight = weight;
  shot->stableSinceAt = now;
}

static inline GrinderAdaptiveSafetyResult grinderAdaptiveSafetyCalculate(float currentSafetyGrams,
                                                                         float targetGrams,
                                                                         const GrinderAdaptiveShot *shot) {
  GrinderAdaptiveSafetyResult result;
  result.recommendationGrams = currentSafetyGrams;
  if (shot == nullptr || !isfinite(targetGrams) || !isfinite(currentSafetyGrams)) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_INACTIVE;
    return result;
  }
  if (!shot->active) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_INACTIVE;
    return result;
  }
  if (!shot->valid) {
    result.skipReason = shot->skipReason == GRINDER_ADAPTIVE_SKIP_NONE ? GRINDER_ADAPTIVE_SKIP_INVALID : shot->skipReason;
    return result;
  }
  if (!shot->finalWeightLocked) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_NOT_FINAL;
    return result;
  }
  if (shot->finalAt <= shot->startAt) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_DURATION;
    return result;
  }
  const uint32_t durationMs = shot->finalAt - shot->startAt;
  if (durationMs < GRINDER_ADAPTIVE_MIN_DURATION_MS) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_DURATION;
    return result;
  }
  const float spanGrams = shot->finalWeight - shot->startWeight;
  if (!isfinite(spanGrams) || spanGrams < GRINDER_ADAPTIVE_MIN_SPAN_GRAMS) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_SPAN;
    return result;
  }
  result.averageRateGps = spanGrams / ((float)durationMs / 1000.0f);
  if (result.averageRateGps <= 0.0f || result.averageRateGps > GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_RATE;
    return result;
  }
  result.errorGrams = shot->finalWeight - targetGrams;
  if (!isfinite(result.errorGrams) || fabsf(result.errorGrams) > GRINDER_ADAPTIVE_MAX_TARGET_ERROR_GRAMS) {
    result.skipReason = GRINDER_ADAPTIVE_SKIP_ERROR;
    return result;
  }
  const float boundedError = grinderClampFloat(result.errorGrams, -GRINDER_ADAPTIVE_MAX_SINGLE_STEP_GRAMS, GRINDER_ADAPTIVE_MAX_SINGLE_STEP_GRAMS);
  result.recommendationGrams = grinderClampFloat(currentSafetyGrams + boundedError, 0.0f, 10.0f);
  result.valid = true;
  return result;
}

#endif
