#ifndef FILESYSTEM_RECOVERY_H
#define FILESYSTEM_RECOVERY_H

#include "parameter.h"
#include <Preferences.h>

bool filesystemRecoveryStore(bool active) {
  Preferences preferences;
  if (!preferences.begin("ota_recovery", false)) return false;
  const bool stored = preferences.putBool("active", active) > 0;
  preferences.end();
  if (stored) filesystemRecoveryActive.store(active);
  return stored;
}

void filesystemRecoveryLoad() {
  Preferences preferences;
  if (!preferences.begin("ota_recovery", true)) return;
  filesystemRecoveryActive.store(preferences.getBool("active", false));
  preferences.end();
}

#endif
