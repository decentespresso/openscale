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
    state = State::Settling;
    baseline = weight;
    previousWeight = weight;
    valley = weight;
    peak = weight;
    riseOrigin = weight;
    strongestHeight = 0.0f;
    riseStartedMs = now;
    lastTapStartedMs = now;
    lastPeakMs = now;
    releaseLevel = weight;
    finalReleased = false;
    lockedMs = now;
    sequenceCount = 0;
  }

  TapEvent tick(unsigned long now, float weight) {
    if (!isfinite(weight)) {
      reset(now, 0.0f);
      return TapEvent::None;
    }
    if (state == State::Locked) {
      if (now - lockedMs > interTapWindowMs) reset(now, weight);
      return TapEvent::None;
    }
    if ((state == State::Rising && now - riseStartedMs > maxTapDurationMs) ||
        (state == State::Valley && !finalReleased &&
         now - lastTapStartedMs > maxTapDurationMs)) {
      reset(now, weight);
      return TapEvent::None;
    }
    if (state == State::Valley) {
      const TapEvent event = updateValley(now, weight);
      if (event != TapEvent::None || sequenceCount == 3) return event;
      if (now - lastPeakMs > interTapWindowMs &&
          (finalReleased || weight - previousWeight > peakSlopeG)) {
        const TapEvent expired = sequenceCount == 2 && finalReleased ?
            TapEvent::Double : TapEvent::None;
        reset(now, weight);
        return expired;
      }
    }

    TapEvent event = TapEvent::None;
    if ((state == State::Ready || state == State::Valley) &&
        weight - previousWeight > peakSlopeG) {
      riseOrigin = state == State::Ready ? baseline : valley;
      peak = weight;
      riseStartedMs = now;
      state = State::Rising;
    }
    if (state == State::Rising) {
      event = updatePeak(now, weight);
    } else if (state == State::Settling || state == State::Ready) {
      updateSteadyState(now, weight);
    }

    previousWeight = weight;
    return event;
  }

 private:
  enum class State : uint8_t { Settling, Ready, Rising, Valley, Locked };

  static constexpr float minPeakHeightG = 10.0f;
  static constexpr float peakSlopeG = 2.0f;
  static constexpr float minDropAfterPeakG = 6.0f;
  static constexpr float minRepeatHeightG = 6.0f;
  static constexpr float minDropFraction = 0.45f;
  static constexpr float finalReleaseFraction = 0.70f;
  static constexpr float minRepeatHeightFraction = 0.35f;
  static constexpr unsigned long minTapSeparationMs = 50;
  static constexpr unsigned long interTapWindowMs = 400;
  static constexpr unsigned long maxTapDurationMs = 600;
  static constexpr unsigned long sampleIntervalMs = 100;
  static constexpr uint8_t steadySampleCount = 5;
  static constexpr float steadyRangeG = 0.5f;

  float history[steadySampleCount];
  uint8_t historyIndex;
  uint8_t historyCount;
  unsigned long sampleMs;
  State state;
  float baseline;
  float previousWeight;
  float valley;
  float peak;
  float riseOrigin;
  float strongestHeight;
  unsigned long riseStartedMs;
  unsigned long lastTapStartedMs;
  unsigned long lastPeakMs;
  float releaseLevel;
  bool finalReleased;
  unsigned long lockedMs;
  uint8_t sequenceCount;

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
      state = State::Ready;
      baseline = sum / steadySampleCount;
    } else if (fabsf(weight - baseline) > peakSlopeG) {
      state = State::Settling;
    }
  }

  TapEvent updatePeak(unsigned long now, float weight) {
    peak = fmaxf(peak, weight);
    const float amplitude = peak - riseOrigin;
    const float height = peak - baseline;
    const float requiredAmplitude = sequenceCount == 0 ? minPeakHeightG : minRepeatHeightG;
    const bool prominent = amplitude > requiredAmplitude &&
        height > strongestHeight * minRepeatHeightFraction;
    if (!prominent && peak - weight > peakSlopeG) {
      if (sequenceCount == 0) {
        reset(now, weight);
      } else {
        valley = weight;
        state = State::Valley;
      }
      return TapEvent::None;
    }
    const float requiredDrop = fmaxf(minDropAfterPeakG, amplitude * minDropFraction);
    if (!prominent || peak - weight < requiredDrop) {
      return TapEvent::None;
    }
    if (sequenceCount > 0 && riseStartedMs - lastTapStartedMs < minTapSeparationMs) {
      valley = weight;
      state = State::Valley;
      return TapEvent::None;
    }
    strongestHeight = fmaxf(strongestHeight, height);
    lastTapStartedMs = riseStartedMs;
    lastPeakMs = now;
    releaseLevel = baseline + fmaxf(2.0f, (peak - baseline) * (1.0f - finalReleaseFraction));
    finalReleased = false;
    ++sequenceCount;
    state = State::Valley;
    valley = weight;
    return updateValley(now, weight);
  }

  TapEvent updateValley(unsigned long now, float weight) {
    valley = fminf(valley, weight);
    if (!finalReleased && weight <= releaseLevel) {
      finalReleased = true;
      lastPeakMs = now;
    }
    if (sequenceCount == 3 && finalReleased) {
      state = State::Locked;
      lockedMs = now;
      return TapEvent::Triple;
    }
    return TapEvent::None;
  }
};

#endif
