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

#endif
