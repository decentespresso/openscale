#ifndef GRINDER_SETTINGS_PERSISTENCE_H
#define GRINDER_SETTINGS_PERSISTENCE_H

static inline void grinderMarkSettingsDirty() {
  grinderRuntime.settingsDirty = true;
}

static inline void grinderFlushSettingsIfDirty() {
  if (grinderRuntime.settingsDirty) {
    grinderSaveSettings();
  }
}

static inline void grinderSaveLastIpIfChanged(IPAddress ip) {
  if (grinderSettings.lastIp != ip) {
    grinderSettings.lastIp = ip;
    grinderSaveSettings();
  }
}

static inline void grinderSaveLookupHintsIfChanged(const char *hostname, IPAddress ip) {
  const bool hostnameChanged = hostname != nullptr && strcmp(grinderSettings.hostname, hostname) != 0;
  const bool ipChanged = grinderSettings.lastIp != ip;
  if (hostnameChanged) {
    grinderCopyCString(grinderSettings.hostname, sizeof(grinderSettings.hostname), hostname);
  }
  if (ipChanged) {
    grinderSettings.lastIp = ip;
  }
  if (hostnameChanged || ipChanged) {
    grinderSaveSettings();
  }
}

#endif
