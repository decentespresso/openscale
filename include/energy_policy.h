#ifndef ENERGY_POLICY_H
#define ENERGY_POLICY_H

#include <stdint.h>

enum class EnergyFeature : uint8_t {
  SerialQuiet,
  OledRedraw,
  OledStatic,
  LightSleep,
  Count
};

struct EnergySettings {
  volatile uint32_t features = 0;

  bool enabled(EnergyFeature feature) const {
    return (features & featureBit(feature)) != 0;
  }

  bool selected(EnergyFeature feature) const {
    return enabled(feature);
  }

  void select(EnergyFeature feature, bool value) {
    const uint32_t bit = featureBit(feature);
    features = value ? features | bit : features & ~bit;
  }

  static constexpr uint32_t featureBit(EnergyFeature feature) {
    return uint32_t(1) << static_cast<uint8_t>(feature);
  }
};

class EnergyPolicy {
public:
  EnergySettings settings;

  bool featureEnabled(EnergyFeature feature) const {
    return settings.enabled(feature);
  }
};

#endif
