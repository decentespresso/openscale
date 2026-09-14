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

  void reset(unsigned long now, float weight) {
    if (!isfinite(weight)) {
      initialized = false;
      return;
    }
    initialized = true;
    windowStartedAt = now;
    windowStartWeight = weight;
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

    const unsigned long elapsed = now - windowStartedAt;
    const float delta = fabsf(weight - windowStartWeight);
    const bool fastEnough = elapsed == 0 ||
      delta * 1000.0f >= MIN_RATE_G_PER_SEC * static_cast<float>(elapsed);

    if (delta >= MIN_DELTA_G && fastEnough) {
      reset(now, weight);
      return true;
    }
    if (elapsed >= WINDOW_MS) {
      reset(now, weight);
    }
    return false;
  }
};

#endif
