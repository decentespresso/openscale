#ifndef INTEGRATION_SETTINGS_H
#define INTEGRATION_SETTINGS_H

#include "integration_state.h"

constexpr const char *KEY_ACTIVE_INTEGRATION = "integration";

inline bool integrationAvailable(IntegrationMode mode) {
  return integrationModeAvailable(mode,
#if HDS_ENABLE_PRESSENSOR
    true,
#else
    false,
#endif
#if HDS_ENABLE_GRINDER
    true);
#else
    false);
#endif
}

inline bool storeIntegrationMode(IntegrationMode mode) {
  if (!integrationAvailable(mode)) return false;
  const uint8_t value = static_cast<uint8_t>(mode);
  return settingsPreferences.putUChar(KEY_ACTIVE_INTEGRATION, value) == sizeof(value) &&
    settingsPreferences.getUChar(KEY_ACTIVE_INTEGRATION, UINT8_MAX) == value;
}

inline bool legacyIntegrationEnabled(const char *name) {
  Preferences preferences;
  if (!preferences.begin(name, true)) return false;
  const bool enabled = preferences.getType("enabled") == PT_U8 &&
    preferences.getUChar("enabled", 0) == 1;
  preferences.end();
  return enabled;
}

inline bool loadIntegrationSelection() {
  const bool stored = settingsPreferences.isKey(KEY_ACTIVE_INTEGRATION);
  const uint8_t value = settingsPreferences.getType(KEY_ACTIVE_INTEGRATION) == PT_U8
    ? settingsPreferences.getUChar(KEY_ACTIVE_INTEGRATION, UINT8_MAX) : UINT8_MAX;
  const IntegrationMode mode = resolveIntegrationMode(stored, value,
    stored ? false : legacyIntegrationEnabled("pressensor"),
    stored ? false : legacyIntegrationEnabled("grinder"),
    integrationAvailable(IntegrationMode::Pressensor), integrationAvailable(IntegrationMode::Grinder));
#if HDS_ENABLE_GRINDER
  if (!stored && grinderSettings.enabled && grinderSettings.previousWifiOnBootSaved &&
      !storagePutBool(KEY_WIFI_BOOT, grinderSettings.previousWifiOnBoot)) return false;
#endif
  if ((!stored || value != static_cast<uint8_t>(mode)) && !storeIntegrationMode(mode)) return false;
  activeIntegration = mode;
#if HDS_ENABLE_GRINDER
  grinderSettings.enabled = mode == IntegrationMode::Grinder;
#endif
#if HDS_ENABLE_PRESSENSOR
  pressensorSettings.enabled = mode == IntegrationMode::Pressensor;
#endif
  return true;
}

inline bool integrationCanRestart() {
  if (b_ota || b_calibration) return false;
#if HDS_ENABLE_GRINDER
  return integrationRestartSafe(grinderRuntime.shutdownUnconfirmed, grinderRuntime.relayOn,
    grinderRuntime.state == GRINDER_STATE_STOPPING,
    grinderRuntime.pendingCommand == GRINDER_COMMAND_ON || grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF);
#else
  return true;
#endif
}

#endif
