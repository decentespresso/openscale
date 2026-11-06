#ifndef HDS_SCALE_DRIFT_H
#define HDS_SCALE_DRIFT_H

#include <math.h>

void resetDriftCompensation() {
  f_driftCompensation = 0.0f;
  scaleDriftState = {};
}

void setMaxDriftCompensation(float value) {
  f_maxDriftCompensation = value;
  resetDriftCompensation();
  resetTracking();
  resetStableOutput();
}

void updateDriftCompensation(float rawWeight) {
  const unsigned long now = millis();
  if (f_maxDriftCompensation <= 0.0f) {
    resetDriftCompensation();
    return;
  }
  if (!scaleDriftState.ready) {
    scaleDriftState.referenceWeight = rawWeight - f_driftCompensation;
    scaleDriftState.previousWeight = rawWeight;
    scaleDriftState.lastSample = now;
    scaleDriftState.ready = true;
    return;
  }
  if (now - scaleDriftState.lastSample < WEIGHT_BASE_INTERVAL_MS) {
    return;
  }
  scaleDriftState.lastSample = now;
  const float movement = rawWeight - scaleDriftState.previousWeight;
  const float residual = rawWeight - scaleDriftState.referenceWeight - f_driftCompensation;
  scaleDriftState.previousWeight = rawWeight;

  if (fabsf(movement) > SCALE_QUIET_THRESHOLD ||
      fabsf(residual) >= f_maxDriftCompensation ||
      (scaleDriftState.quietSamples < 3 && fabsf(residual) > SCALE_QUIET_THRESHOLD)) {
    scaleDriftState.referenceWeight = rawWeight - f_driftCompensation;
    scaleDriftState.quietSamples = 0;
    scaleDriftState.similarSamples = 0;
    scaleDriftState.lastResidual = 0.0f;
    return;
  }
  if (scaleDriftState.quietSamples < 3) {
    scaleDriftState.quietSamples++;
    if (scaleDriftState.quietSamples == 3) {
      scaleDriftState.referenceWeight = rawWeight - f_driftCompensation;
    }
    return;
  }
  if (fabsf(residual) <= SCALE_QUIET_THRESHOLD) {
    scaleDriftState.similarSamples = 0;
    scaleDriftState.lastResidual = 0.0f;
    return;
  }

  scaleDriftState.similarSamples = scaleDriftState.lastResidual * residual > 0.0f
                                    ? scaleDriftState.similarSamples + 1 : 1;
  scaleDriftState.lastResidual = residual;
  if (scaleDriftState.similarSamples < 3) {
    return;
  }
  f_driftCompensation += residual * 0.3f;
  if (fabsf(f_driftCompensation) > 2.0f) {
    f_driftCompensation = f_driftCompensation > 0.0f ? 2.0f : -2.0f;
  }
  scaleDriftState.similarSamples = 2;
  if (b_weight_in_serial) {
    Serial.print("TEMP-DRIFT-COMP: diff=");
    Serial.print(residual, 4);
    Serial.print("g, total_comp=");
    Serial.println(f_driftCompensation, 4);
  }
}

#endif
