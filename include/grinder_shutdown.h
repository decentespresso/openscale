#ifndef GRINDER_SHUTDOWN_H
#define GRINDER_SHUTDOWN_H

inline void grinderResumeAfterCancelledShutdown() {
  grinderResumeAfterMenu();
}

inline void grinderPrepareForShutdown() {
  grinderRuntime.menuPaused = true;
  if (grinderBleConnected()) {
    grinderRuntime.shutdownUnconfirmed = true;
    if (grinderSendOff()) {
      const unsigned long startedAt = millis();
      while (grinderBleConnected() && grinderRuntime.shutdownUnconfirmed && millis() - startedAt < 250) {
        grinderBleTick();
        grinderReadClient(f_displayedValue);
        if (grinderRuntime.shutdownUnconfirmed) delay(1);
      }
    }
  }
  if (grinderRuntime.shutdownUnconfirmed) Serial.println("[grinder] shutdown OFF not confirmed");
  grinderRuntimeReset();
}

#endif
