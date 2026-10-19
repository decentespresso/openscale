#ifndef AUTO_OFF_ACTIVITY_H
#define AUTO_OFF_ACTIVITY_H

#include <math.h>

struct AutoOffWeightActivityTracker {
  static constexpr unsigned long WINDOW_MS = 1000;
  static constexpr float MIN_DELTA_G = 1.0f;
  static constexpr float MIN_RATE_G_PER_SEC = 1.0f;

  bool initialized = false;
  unsigned long windowStartedAt = 0;
  float windowStartWeight = 0.0f;
  unsigned long previousSampleAt = 0;
  float previousWeight = 0.0f;

  void reset(unsigned long now, float weight) {
    if (!isfinite(weight)) {
      initialized = false;
      return;
    }
    initialized = true;
    windowStartedAt = now;
    windowStartWeight = weight;
    previousSampleAt = now;
    previousWeight = weight;
  }

  bool hasActivity(unsigned long now, float weight) const {
    const unsigned long elapsed = now - windowStartedAt;
    const float delta = fabsf(weight - windowStartWeight);
    const bool fastEnough = elapsed == 0 ||
      delta * 1000.0f >= MIN_RATE_G_PER_SEC * static_cast<float>(elapsed);

    return delta >= MIN_DELTA_G && fastEnough;
  }

  bool update(unsigned long now, float weight) {
    if (!isfinite(weight)) {
      initialized = false;
      return false;
    }
    if (!initialized) {
      reset(now, weight);
      return false;
    }

    if (now - windowStartedAt >= WINDOW_MS && !hasActivity(now, weight)) {
      windowStartedAt = previousSampleAt;
      windowStartWeight = previousWeight;
    }
    if (hasActivity(now, weight)) {
      reset(now, weight);
      return true;
    }
    previousSampleAt = now;
    previousWeight = weight;
    return false;
  }
};

#endif
