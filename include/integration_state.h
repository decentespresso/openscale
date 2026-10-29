#ifndef INTEGRATION_STATE_H
#define INTEGRATION_STATE_H

#include <stdint.h>

enum class IntegrationMode : uint8_t { None, Pressensor, Grinder };

inline bool integrationModeAvailable(IntegrationMode mode, bool pressensor, bool grinder) {
  return mode == IntegrationMode::None ||
    (mode == IntegrationMode::Pressensor && pressensor) ||
    (mode == IntegrationMode::Grinder && grinder);
}

inline IntegrationMode resolveIntegrationMode(bool stored, uint8_t value,
    bool legacyPressensor, bool legacyGrinder, bool pressensor, bool grinder) {
  const IntegrationMode mode = stored ? static_cast<IntegrationMode>(value) :
    legacyPressensor == legacyGrinder ? IntegrationMode::None :
    legacyPressensor ? IntegrationMode::Pressensor : IntegrationMode::Grinder;
  return integrationModeAvailable(mode, pressensor, grinder) ? mode : IntegrationMode::None;
}

inline bool integrationRestartSafe(bool shutdownUnconfirmed, bool relayOn,
    bool stopping, bool commandPending) {
  return !shutdownUnconfirmed && !relayOn && !stopping && !commandPending;
}

#endif
