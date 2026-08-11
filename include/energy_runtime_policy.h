#ifndef ENERGY_RUNTIME_POLICY_H
#define ENERGY_RUNTIME_POLICY_H

#include <stdint.h>

enum class DisplayIdleMode : uint8_t {
  Active,
  Dimmed,
  Off
};

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
  bool shouldRun(bool enabled, uint32_t now, uint32_t intervalMs) {
    if (!enabled) {
      initialized = false;
      return true;
    }
    if (!initialized || now - lastRunAt >= intervalMs) {
      initialized = true;
      lastRunAt = now;
      return true;
    }
    return false;
  }

  void reset() {
    initialized = false;
  }

private:
  bool initialized = false;
  uint32_t lastRunAt = 0;
};

struct EnergyRuntimeSchedule {
  CadenceGate autoOff;
  CadenceGate chargeCheck;
  CadenceGate oledIdle;
};

class MotionSampleGate {
public:
  bool shouldRead(bool enabled, bool fresh, bool available, uint32_t now) {
    if (!available) return false;
    if (!enabled || fresh || !initialized || now - lastReadAt >= 100) {
      initialized = true;
      lastReadAt = now;
      return true;
    }
    return false;
  }

  void reset() {
    initialized = false;
  }

private:
  bool initialized = false;
  uint32_t lastReadAt = 0;
};

class BatterySampleGate {
public:
  bool shouldEvaluate(uint32_t sequence) {
    if (sequence == processedSequence) return false;
    processedSequence = sequence;
    return true;
  }

  void reset(uint32_t sequence) {
    processedSequence = sequence;
  }

private:
  uint32_t processedSequence = 0;
};

struct EnergyRuntimePolicy {
  static constexpr uint8_t lowBatteryConfirmationSamples = 2;

  static bool lowBatteryConfirmed(uint8_t samples) {
    return samples >= lowBatteryConfirmationSamples;
  }

  static bool meaningfulWeightChange(float previous, float current, float threshold) {
    const float change = current - previous;
    return change >= threshold || change <= -threshold;
  }

  static bool shouldServiceFeature(uint32_t enabledMask, uint32_t featureBit) {
    return enabledMask != 0 && (enabledMask & featureBit) != 0;
  }

  static DisplayIdleMode displayMode(bool enabled, bool inhibited, bool explicitlyOff,
                                     uint32_t inactiveMs) {
    if (explicitlyOff) return DisplayIdleMode::Off;
    if (!enabled || inhibited) return DisplayIdleMode::Active;
    if (inactiveMs >= 120000) return DisplayIdleMode::Off;
    if (inactiveMs >= 30000) return DisplayIdleMode::Dimmed;
    return DisplayIdleMode::Active;
  }

  static uint8_t displayContrast(uint8_t requested, DisplayIdleMode mode) {
    return mode == DisplayIdleMode::Dimmed && requested > 32 ? 32 : requested;
  }
};

#endif
