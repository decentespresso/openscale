#ifndef GRINDER_RUNTIME_LOW_LATENCY_H
#define GRINDER_RUNTIME_LOW_LATENCY_H

#ifndef GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS
#define GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500
#endif

#ifndef GRINDER_CONFIRM_MIN_DURATION_MS
#define GRINDER_CONFIRM_MIN_DURATION_MS 1500
#endif

#ifndef GRINDER_CONFIRM_MIN_RISE_GRAMS
#define GRINDER_CONFIRM_MIN_RISE_GRAMS 2.0f
#endif

#ifndef GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES
#define GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES 4
#endif

#ifndef GRINDER_CONFIRM_MIN_STEP_GRAMS
#define GRINDER_CONFIRM_MIN_STEP_GRAMS 0.03f
#endif

#ifndef GRINDER_CONFIRM_MAX_INITIAL_GRAMS
#define GRINDER_CONFIRM_MAX_INITIAL_GRAMS 3.0f
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

static inline void grinderResetGrindConfirmation() {
  grinderRuntime.grindConfirmed = false;
  grinderRuntime.grindCandidateActive = false;
  grinderRuntime.setupMassBlocked = false;
  grinderRuntime.grindCandidateStartAt = 0;
  grinderRuntime.grindCandidateStartWeight = 0.0f;
  grinderRuntime.grindCandidateLastWeight = 0.0f;
  grinderRuntime.grindCandidatePositiveSamples = 0;
}

static inline void grinderRuntimeNotifyTareRequested() {
  if (!grinderSettings.enabled || grinderRuntime.state == GRINDER_STATE_DISABLED) {
    return;
  }
  grinderRuntime.tarePending = true;
  grinderSetStatus("tare wait");
}

static inline void grinderRuntimeNotifyTareComplete() {
  if (!grinderSettings.enabled) {
    grinderRuntime.tarePending = false;
    return;
  }
  grinderRuntime.tarePending = false;
  grinderRuntime.grindRateGps = 0.0f;
  grinderRuntime.rateSamples = 0;
  grinderRuntime.lastWeight = 0.0f;
  grinderRuntime.lastWeightAt = millis();
  grinderRuntime.stopWeight = 0.0f;
  grinderResetCutoffGuard();
  grinderResetGrindConfirmation();
  grinderAdaptiveShotReset(&grinderRuntime.adaptiveShot);
  if (grinderRuntime.state == GRINDER_STATE_GRINDING) {
    grinderSetStatus("grinding");
  }
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

static inline void grinderStartGrindCandidate(float weight, uint32_t now) {
  grinderRuntime.grindCandidateActive = true;
  grinderRuntime.grindCandidateStartAt = now;
  grinderRuntime.grindCandidateStartWeight = grinderSettings.zeroMaxGrams;
  grinderRuntime.grindCandidateLastWeight = weight;
  grinderRuntime.grindCandidatePositiveSamples = 0;
}

static inline bool grinderCandidateRateValid(float weight, uint32_t now) {
  if (!grinderRuntime.grindCandidateActive || now <= grinderRuntime.grindCandidateStartAt) {
    return false;
  }
  const float durationSeconds = (now - grinderRuntime.grindCandidateStartAt) / 1000.0f;
  const float rise = weight - grinderRuntime.grindCandidateStartWeight;
  if (durationSeconds <= 0.0f || rise <= 0.0f) {
    return false;
  }
  const float averageRate = rise / durationSeconds;
  return averageRate > 0.0f && averageRate <= GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS;
}

static inline bool grinderTrackGrindConfirmation(float weight, uint32_t now) {
  if (grinderRuntime.grindConfirmed) {
    return true;
  }
  if (!grinderRuntime.grindCandidateActive) {
    if (weight > grinderSettings.zeroMaxGrams + GRINDER_CONFIRM_MAX_INITIAL_GRAMS) {
      grinderRuntime.setupMassBlocked = true;
      grinderSetStatus("tare cup");
      return false;
    }
    grinderStartGrindCandidate(weight, now);
    return false;
  }
  if (weight > grinderRuntime.grindCandidateLastWeight + GRINDER_CONFIRM_MIN_STEP_GRAMS &&
      grinderRuntime.grindCandidatePositiveSamples < 255) {
    grinderRuntime.grindCandidatePositiveSamples++;
  }
  grinderRuntime.grindCandidateLastWeight = weight;
  const uint32_t duration = now - grinderRuntime.grindCandidateStartAt;
  const float rise = weight - grinderRuntime.grindCandidateStartWeight;
  if (duration < GRINDER_CONFIRM_MIN_DURATION_MS ||
      rise < GRINDER_CONFIRM_MIN_RISE_GRAMS ||
      grinderRuntime.grindCandidatePositiveSamples < GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES ||
      !grinderCandidateRateValid(weight, now)) {
    return false;
  }
  grinderRuntime.grindConfirmed = true;
  grinderRuntime.setupMassBlocked = false;
  grinderSetStatus("grinding");
  return true;
}

static inline bool grinderCutoffEligible(float weight, uint32_t now, float cutoff, bool protectedByZeroExit) {
  if (grinderRuntime.tarePending) {
    return false;
  }
  if (grinderWeightInZeroRange(weight)) {
    grinderResetGrindConfirmation();
    return false;
  }
  if (grinderRuntime.setupMassBlocked) {
    grinderSetStatus("tare cup");
    return false;
  }
  if (grinderTrackGrindConfirmation(weight, now)) {
    return true;
  }
  if (weight >= cutoff && !protectedByZeroExit) {
    grinderRuntime.setupMassBlocked = true;
    grinderSetStatus("tare cup");
  }
  return false;
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
  const bool eligible = grinderCutoffEligible(weight, now, cutoff, protectedByZeroExit);
  if (weight < cutoff || protectedByZeroExit || !eligible) {
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
