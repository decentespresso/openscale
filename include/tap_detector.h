#ifndef TAP_DETECTOR_H
#define TAP_DETECTOR_H

#include <math.h>
#include <stdint.h>

enum class TapEvent : uint8_t {
  None,
  Double,
  Triple
};

class TapDetector {
 public:
  TapDetector() { reset(0, 0.0f); }

  void reset(unsigned long now, float weight) {
    for (float &sample : history) sample = weight;
    historyIndex = 0;
    historyCount = 0;
    sampleMs = now;
    steady = false;
    baseline = weight;
    previousWeight = weight;
    rising = false;
    risingFromSteady = false;
    lastPeakMs = now;
    sequenceCount = 0;
    needsRelease = false;
    sequenceLocked = false;
  }

  TapEvent tick(unsigned long now, float weight) {
    updateSteadyState(now, weight);

    if (sequenceLocked) {
      if (now - lastPeakMs <= doubleWindowMs) {
        previousWeight = weight;
        rising = false;
        return TapEvent::None;
      }
      sequenceLocked = false;
      sequenceCount = 0;
    }

    TapEvent event = expireSequence(now);

    if (needsRelease && fabsf(weight - baseline) <= releaseRangeG) {
      needsRelease = false;
    }

    if (weight > previousWeight + peakSlopeG &&
        !needsRelease && (sequenceCount > 0 || steady)) {
      rising = true;
      risingFromSteady = steady;
    } else if (weight < previousWeight - peakSlopeG && rising) {
      rising = false;
      if (previousWeight - baseline > peakHeightG) {
        event = acceptPeak(now, risingFromSteady);
      }
    }

    previousWeight = weight;
    return event;
  }

 private:
  static constexpr float peakHeightG = 20.0f;
  static constexpr float peakSlopeG = 2.0f;
  static constexpr float releaseRangeG = 2.0f;
  static constexpr unsigned long sampleIntervalMs = 100;
  static constexpr uint8_t steadySampleCount = 5;
  static constexpr float steadyRangeG = 0.5f;
  static constexpr unsigned long doubleWindowMs = 400;

  float history[steadySampleCount];
  uint8_t historyIndex;
  uint8_t historyCount;
  unsigned long sampleMs;
  bool steady;
  float baseline;
  float previousWeight;
  bool rising;
  bool risingFromSteady;
  unsigned long lastPeakMs;
  uint8_t sequenceCount;
  bool needsRelease;
  bool sequenceLocked;

  void updateSteadyState(unsigned long now, float weight) {
    if (now - sampleMs < sampleIntervalMs) return;

    sampleMs = now;
    history[historyIndex] = weight;
    historyIndex = (historyIndex + 1) % steadySampleCount;
    if (historyCount < steadySampleCount) ++historyCount;
    if (historyCount < steadySampleCount) return;

    float low = history[0];
    float high = history[0];
    float sum = 0.0f;
    for (const float sample : history) {
      if (sample < low) low = sample;
      if (sample > high) high = sample;
      sum += sample;
    }
    if (high - low < steadyRangeG) {
      steady = true;
      baseline = sum / steadySampleCount;
    } else if (fabsf(weight - baseline) > peakSlopeG) {
      steady = false;
    }
  }

  TapEvent expireSequence(unsigned long now) {
    if (sequenceCount == 0 || now - lastPeakMs < doubleWindowMs) {
      return TapEvent::None;
    }
    if (sequenceCount == 2) {
      sequenceCount = 0;
      return TapEvent::Double;
    }
    sequenceCount = 0;
    return TapEvent::None;
  }

  TapEvent acceptPeak(unsigned long now, bool startedFromSteady) {
    needsRelease = true;
    if (sequenceCount == 0) {
      if (!startedFromSteady) return TapEvent::None;
      sequenceCount = 1;
    } else if (now - lastPeakMs < doubleWindowMs) {
      ++sequenceCount;
    } else {
      sequenceCount = startedFromSteady ? 1 : 0;
    }
    lastPeakMs = now;

    if (sequenceCount == 3) {
      sequenceCount = 0;
      sequenceLocked = true;
      return TapEvent::Triple;
    }
    return TapEvent::None;
  }
};

#endif
