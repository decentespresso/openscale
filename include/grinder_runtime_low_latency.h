#ifndef GRINDER_RUNTIME_LOW_LATENCY_H
#define GRINDER_RUNTIME_LOW_LATENCY_H

#ifndef GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS
#define GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500
#endif

static inline bool grinderActivePlugValidated() {
  return grinderIsMac(grinderSettings.selectedMac) &&
         strcmp(grinderRuntime.activePlugMac, grinderSettings.selectedMac) == 0;
}

static inline bool grinderWriteFastOff() {
  if (!grinderRuntime.client.connected() || !grinderActivePlugValidated()) {
    return false;
  }
  const uint8_t command[] = { '!', '\n' };
  const size_t written = grinderRuntime.client.write(command, sizeof(command));
  if (written != sizeof(command)) {
    return false;
  }
  grinderRuntime.pendingCommand = GRINDER_COMMAND_OFF;
  grinderRuntime.lastCommandAt = millis();
  Serial.printf("[grinder] tx ! state=%s\n", grinderStateText(grinderRuntime.state));
  return true;
}

static inline bool grinderSendOff() {
  if (!grinderRuntime.client.connected()) {
    return false;
  }
  if (grinderWriteFastOff()) {
    return true;
  }
  return grinderSendSimpleCommand("OFF", GRINDER_COMMAND_OFF);
}

static inline void grinderMaintainWifiLatencyMode() {
  const bool active = grinderSettings.enabled &&
                      WiFi.status() == WL_CONNECTED &&
                      grinderRuntime.state != GRINDER_STATE_DISABLED &&
                      grinderRuntime.state != GRINDER_STATE_FINDING_PLUG &&
                      grinderRuntime.state != GRINDER_STATE_ERROR;
  if (active) {
    if (!grinderRuntime.wifiLowLatency) {
      WiFi.setSleep(false);
      if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
        grinderRuntime.wifiLowLatency = true;
      }
    }
    return;
  }
  if (grinderRuntime.wifiLowLatency) {
    WiFi.setSleep(true);
    if (esp_wifi_set_ps(WIFI_PS_MIN_MODEM) == ESP_OK) {
      grinderRuntime.wifiLowLatency = false;
    }
  }
}

static inline bool grinderRuntimeLocksScaleSampling() {
  return grinderSettings.enabled &&
         (grinderRuntime.state == GRINDER_STATE_GRINDING ||
          grinderRuntime.state == GRINDER_STATE_STOPPING);
}

static inline void grinderResetCutoffGuard() {
  grinderRuntime.cutoffGuardActive = false;
  grinderRuntime.cutoffGuardZeroExitAt = 0;
}

static inline bool grinderCutoffProtected(float weight, uint32_t now) {
  if (grinderWeightInZeroRange(weight)) {
    grinderResetCutoffGuard();
    return true;
  }
  if (!grinderRuntime.cutoffGuardActive) {
    grinderRuntime.cutoffGuardActive = true;
    grinderRuntime.cutoffGuardZeroExitAt = now;
    return true;
  }
  return now - grinderRuntime.cutoffGuardZeroExitAt < GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS;
}

static inline void grinderTickGrindingCutoff(float weight) {
  if (grinderRuntime.state != GRINDER_STATE_GRINDING) {
    return;
  }
  const uint32_t now = millis();
  grinderUpdateRate(weight);
  const float rate = grinderRuntime.rateSamples > 0 ? grinderRuntime.grindRateGps : 0.0f;
  const float cutoff = grinderCutoffGrams(grinderSettings.targetGrams, grinderSettings.safetyMarginGrams);
  const bool protectedByZeroExit = grinderCutoffProtected(weight, now);
  if (weight < cutoff || protectedByZeroExit) {
    return;
  }
  const uint32_t guardAge = now - grinderRuntime.cutoffGuardZeroExitAt;
  grinderRuntime.stopWeight = weight;
  if (!grinderSendOff()) {
    grinderEnterError("off send failed");
    return;
  }
  grinderSetStatus("stopping");
  grinderSetState(GRINDER_STATE_STOPPING);
  Serial.printf("[grinder] cutoff weight=%.2f target=%.2f rate=%.2f cutoff=%.2f safety=%.2f guard=%lu\n",
                weight,
                grinderSettings.targetGrams,
                rate,
                cutoff,
                grinderSettings.safetyMarginGrams,
                (unsigned long)guardAge);
}

static inline void grinderRuntimeFreshWeightTick(float weight, uint32_t sampleSequence) {
  if (sampleSequence == 0 || grinderRuntime.lastFastWeightSequence == sampleSequence) {
    return;
  }
  grinderRuntime.lastFastWeightSequence = sampleSequence;
  if (!grinderSettings.enabled || grinderRuntime.state != GRINDER_STATE_GRINDING) {
    return;
  }
  grinderTickGrindingCutoff(weight);
}

#endif
