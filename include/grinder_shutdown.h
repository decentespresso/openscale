#ifndef GRINDER_SHUTDOWN_H
#define GRINDER_SHUTDOWN_H

inline void grinderResumeAfterCancelledShutdown() {
  grinderResumeAfterMenu();
}

inline void grinderPrepareForShutdown() {
  grinderRuntime.menuPaused = true;
  if (grinderBleConnected() && grinderSendOff()) {
    const unsigned long startedAt = millis();
    while (grinderBleConnected() && grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF &&
           millis() - startedAt < 250) {
      grinderBleTick();
      grinderReadClient(f_displayedValue);
      if (grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF) delay(1);
    }
  }
  if (grinderRuntime.shutdownUnconfirmed) Serial.println("[grinder] shutdown OFF not confirmed");
  grinderRuntimeReset();
}

#endif
