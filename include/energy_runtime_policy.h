#ifndef ENERGY_RUNTIME_POLICY_H
#define ENERGY_RUNTIME_POLICY_H

#include <stdint.h>

class OledFrameGate {
public:
  bool shouldRender(bool enabled, uint32_t signature, uint32_t now,
                    uint32_t safetyRefreshMs = 1000) {
    const bool safetyRefresh = enabled && initialized && signature == lastSignature &&
                               now - lastRenderAt >= safetyRefreshMs;
    const bool render = !enabled || !initialized || signature != lastSignature || safetyRefresh;
    if (render) {
      initialized = true;
      lastSignature = signature;
      lastRenderAt = now;
    }
    return render;
  }

  void invalidate() {
    initialized = false;
  }

private:
  bool initialized = false;
  uint32_t lastSignature = 0;
  uint32_t lastRenderAt = 0;
};

class CadenceGate {
public:
  bool shouldRun(uint32_t now, uint32_t intervalMs) {
    if (!initialized || now - lastRunAt >= intervalMs) {
      initialized = true;
      lastRunAt = now;
      return true;
    }
    return false;
  }

private:
  bool initialized = false;
  uint32_t lastRunAt = 0;
};

class BatterySampleGate {
public:
  bool shouldEvaluate(uint32_t sequence) {
    if (sequence == processedSequence) return false;
    processedSequence = sequence;
    return true;
  }

private:
  uint32_t processedSequence = 0;
};

struct EnergyRuntimePolicy {
  static constexpr uint8_t lowBatteryConfirmationSamples = 2;

  static uint32_t timeUntil(uint32_t now, uint32_t last, uint32_t interval) {
    const uint32_t elapsed = now - last;
    return elapsed >= interval ? 0 : interval - elapsed;
  }

  static uint32_t earlier(uint32_t current, uint32_t candidate) {
    return candidate < current ? candidate : current;
  }

  static bool lowBatteryConfirmed(uint32_t samples) {
    return samples >= lowBatteryConfirmationSamples;
  }

  static uint8_t batteryDisplayBucket(int percent) {
    if (percent <= 5) return 0;
    if (percent <= 25) return 1;
    if (percent <= 50) return 2;
    if (percent <= 75) return 3;
    return 4;
  }
};

#endif
