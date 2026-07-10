#ifndef GRINDER_RUNTIME_LOW_LATENCY_H
#define GRINDER_RUNTIME_LOW_LATENCY_H

#define GRINDER_CONFIRM_MIN_DURATION_MS 1500
#define GRINDER_CONFIRM_MIN_RISE_GRAMS 2.0f
#define GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES 4
#define GRINDER_CONFIRM_MIN_STEP_GRAMS 0.03f

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

static inline bool grinderRuntimeLocksScaleSampling() {
  return grinderSettings.enabled &&
         (grinderRuntime.state == GRINDER_STATE_GRINDING ||
          grinderRuntime.state == GRINDER_STATE_STOPPING);
}

static inline void grinderResetCutoffGuard() {
  grinderRuntime.cutoffGuardActive = false;
  grinderRuntime.cutoffGuardZeroExitAt = 0;
  grinderRuntime.setupMassBlocked = false;
}

static inline void grinderResetGrindConfirmation() {
  grinderRuntime.grindConfirmed = false;
  grinderRuntime.grindCandidateActive = false;
  grinderRuntime.grindCandidateStartAt = 0;
  grinderRuntime.grindCandidateStartWeight = 0.0f;
  grinderRuntime.grindCandidateLastWeight = 0.0f;
  grinderRuntime.grindCandidatePositiveSamples = 0;
}

static inline void grinderRuntimeNotifyTareRequested(bool userRequested) {
  if (!grinderSettings.enabled || grinderRuntime.state == GRINDER_STATE_DISABLED) {
    return;
  }
  grinderRuntime.tarePending = true;
  grinderRuntime.tareRequestArmsGrinder = grinderRuntime.tareRequestArmsGrinder || userRequested;
  grinderSetStatus("tare wait");
}

static inline void grinderRuntimeNotifyTareComplete() {
  if (!grinderSettings.enabled) {
    grinderRuntime.tarePending = false;
    return;
  }
  const bool userRequested = grinderRuntime.tareRequestArmsGrinder;
  grinderRuntime.tarePending = false;
  grinderRuntime.tareRequestArmsGrinder = false;
  if (userRequested) {
    grinderRuntime.userTareComplete = true;
  }
  grinderRuntime.grindRateGps = 0.0f;
  grinderRuntime.rateSamples = 0;
  grinderRuntime.lastWeight = 0.0f;
  grinderRuntime.lastWeightAt = millis();
  grinderRuntime.stopWeight = 0.0f;
  grinderResetCutoffGuard();
  grinderResetGrindConfirmation();
  grinderAdaptiveShotReset(&grinderRuntime.adaptiveShot);
  if (grinderRuntime.state == GRINDER_STATE_GRINDING) {
    grinderSetStatus("ready");
  } else if (userRequested && grinderRuntime.state == GRINDER_STATE_STOPPING) {
    grinderRuntime.tareRearmRequested = true;
  } else if (userRequested &&
             (grinderRuntime.state == GRINDER_STATE_AWAIT_REMOVAL || grinderRuntime.state == GRINDER_STATE_AWAIT_ZERO)) {
    grinderRuntime.tareRearmRequested = false;
    grinderSetStatus("zero wait");
    grinderSetState(GRINDER_STATE_CONNECTED);
  } else if (grinderRuntime.state == GRINDER_STATE_CONNECTED) {
    grinderSetStatus(grinderRuntime.userTareComplete ? "zero wait" : "tare to arm");
  }
}

static inline void grinderStartGrindCandidate(float weight, uint32_t now) {
  grinderRuntime.grindCandidateActive = true;
  grinderRuntime.grindCandidateStartAt = now;
  grinderRuntime.grindCandidateStartWeight = grinderSettings.zeroMaxGrams;
  grinderRuntime.grindCandidateLastWeight = weight;
  grinderRuntime.grindCandidatePositiveSamples = 0;
}

static inline bool grinderTrackGrindConfirmation(float weight, uint32_t now) {
  if (grinderRuntime.grindConfirmed) {
    return true;
  }
  if (!grinderWeightInPositiveDoseRange(weight)) {
    return false;
  }
  if (!grinderRuntime.grindCandidateActive) {
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
      grinderRuntime.grindCandidatePositiveSamples < GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES) {
    return false;
  }
  grinderRuntime.grindConfirmed = true;
  grinderSetStatus("grinding");
  return true;
}

static inline void grinderTickGrindingCutoff(float weight) {
  if (grinderRuntime.state != GRINDER_STATE_GRINDING) {
    return;
  }
  const uint32_t now = millis();
  grinderUpdateRate(weight);
  const float rate = grinderRuntime.rateSamples > 0 ? grinderRuntime.grindRateGps : 0.0f;
  const float cutoff = grinderCutoffGrams(grinderSettings.targetGrams, grinderSettings.safetyMarginGrams);
  if (!grinderWeightInPositiveDoseRange(weight)) {
    grinderResetGrindConfirmation();
  } else {
    grinderTrackGrindConfirmation(weight, now);
  }
  const bool setupMassWasBlocked = grinderRuntime.setupMassBlocked;
  if (!grinderCutoffShouldStop(weight,
                               grinderSettings.zeroMaxGrams,
                               grinderSettings.targetGrams,
                               grinderSettings.safetyMarginGrams,
                               grinderRuntime.tarePending,
                               now,
                               &grinderRuntime.cutoffGuardActive,
                               &grinderRuntime.cutoffGuardZeroExitAt,
                               &grinderRuntime.setupMassBlocked)) {
    if (grinderRuntime.setupMassBlocked) {
      grinderSetStatus("tare cup");
    } else if (setupMassWasBlocked) {
      grinderSetStatus("ready");
    }
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
  if (grinderPlugConnectionStale(millis())) {
    grinderEnterError("lost plug");
    return;
  }
  grinderTickGrindingCutoff(weight);
}

#endif
