#ifndef CALIBRATION_VALIDATION_H
#define CALIBRATION_VALIDATION_H

#include <math.h>
#include <stdint.h>

const float CALIBRATION_VALUE_DEFAULT = 1000.0f;
const float CALIBRATION_VALUE_MIN_ABS = 1.0f;
const float CALIBRATION_VALUE_MAX_ABS = 1000000.0f;
const float CALIBRATION_KNOWN_MASS_MIN_G = 1.0f;
const float CALIBRATION_VERIFY_MIN_TOLERANCE_G = 0.5f;
const float CALIBRATION_VERIFY_TOLERANCE_RATIO = 0.03f;
const float CALIBRATION_STABILITY_MAX_G = 0.5f;
const uint8_t CALIBRATION_MIN_VALID_SAMPLES = 16;
const uint8_t CALIBRATION_STABLE_READS_REQUIRED = 3;
const unsigned long CALIBRATION_CAPTURE_TIMEOUT_MS = 8000;
const unsigned long CALIBRATION_STABLE_HOLD_MS = 1000;

#ifndef CALIBRATION_ALLOW_NEGATIVE_FACTOR
#define CALIBRATION_ALLOW_NEGATIVE_FACTOR 0
#endif

enum CalibrationRejectReason {
  CAL_REJECT_NONE = 0,
  CAL_REJECT_ADC_TIMEOUT,
  CAL_REJECT_INSUFFICIENT_SAMPLES,
  CAL_REJECT_ADC_RANGE,
  CAL_REJECT_UNSTABLE_ZERO,
  CAL_REJECT_UNSTABLE_LOAD,
  CAL_REJECT_UNKNOWN_MASS,
  CAL_REJECT_RAW_DELTA_TOO_SMALL,
  CAL_REJECT_FACTOR_NEAR_ZERO,
  CAL_REJECT_FACTOR_TOO_LARGE,
  CAL_REJECT_FACTOR_SIGN,
  CAL_REJECT_VERIFY_INVERTED,
  CAL_REJECT_VERIFY_TOLERANCE,
  CAL_REJECT_SMART_CAL_DISABLED,
  CAL_REJECT_UNPLUG_USB
};

inline const char *calibrationRejectReasonText(CalibrationRejectReason reason) {
  switch (reason) {
    case CAL_REJECT_NONE: return "ok";
    case CAL_REJECT_ADC_TIMEOUT: return "adc_timeout";
    case CAL_REJECT_INSUFFICIENT_SAMPLES: return "insufficient_samples";
    case CAL_REJECT_ADC_RANGE: return "adc_out_of_range";
    case CAL_REJECT_UNSTABLE_ZERO: return "unstable_zero";
    case CAL_REJECT_UNSTABLE_LOAD: return "unstable_load";
    case CAL_REJECT_UNKNOWN_MASS: return "unknown_mass";
    case CAL_REJECT_RAW_DELTA_TOO_SMALL: return "raw_delta_too_small";
    case CAL_REJECT_FACTOR_NEAR_ZERO: return "factor_near_zero";
    case CAL_REJECT_FACTOR_TOO_LARGE: return "factor_too_large";
    case CAL_REJECT_FACTOR_SIGN: return "factor_sign";
    case CAL_REJECT_VERIFY_INVERTED: return "verify_inverted";
    case CAL_REJECT_VERIFY_TOLERANCE: return "verify_tolerance";
    case CAL_REJECT_SMART_CAL_DISABLED: return "smart_cal_disabled";
    case CAL_REJECT_UNPLUG_USB: return "unplug_usb";
    default: return "unknown";
  }
}

inline const char *calibrationRejectReasonDisplayText(CalibrationRejectReason reason) {
  switch (reason) {
    case CAL_REJECT_NONE: return "OK";
    case CAL_REJECT_ADC_TIMEOUT: return "ADC timeout";
    case CAL_REJECT_INSUFFICIENT_SAMPLES: return "Not enough samples";
    case CAL_REJECT_ADC_RANGE: return "ADC out of range";
    case CAL_REJECT_UNSTABLE_ZERO: return "Unstable zero";
    case CAL_REJECT_UNSTABLE_LOAD: return "Unstable load";
    case CAL_REJECT_UNKNOWN_MASS: return "Unknown mass";
    case CAL_REJECT_RAW_DELTA_TOO_SMALL: return "No weight change";
    case CAL_REJECT_FACTOR_NEAR_ZERO: return "Factor near zero";
    case CAL_REJECT_FACTOR_TOO_LARGE: return "Factor too large";
    case CAL_REJECT_FACTOR_SIGN: return "Wrong sign";
    case CAL_REJECT_VERIFY_INVERTED: return "Inverted weight";
    case CAL_REJECT_VERIFY_TOLERANCE: return "Verify failed";
    case CAL_REJECT_SMART_CAL_DISABLED: return "Smart cal off";
    case CAL_REJECT_UNPLUG_USB: return "Unplug USB";
    default: return "unknown";
  }
}

inline bool calibrationFactorSignAllowed(float value) {
#if CALIBRATION_ALLOW_NEGATIVE_FACTOR
  return value != 0.0f;
#else
  return value > 0.0f;
#endif
}

inline bool isValidCalibrationValue(float value) {
  float magnitude = fabsf(value);
  return isfinite(value) &&
         calibrationFactorSignAllowed(value) &&
         magnitude >= CALIBRATION_VALUE_MIN_ABS &&
         magnitude <= CALIBRATION_VALUE_MAX_ABS;
}

inline float calibrationVerifyTolerance(float knownMass) {
  float ratioTolerance = fabsf(knownMass) * CALIBRATION_VERIFY_TOLERANCE_RATIO;
  return ratioTolerance > CALIBRATION_VERIFY_MIN_TOLERANCE_G
           ? ratioTolerance
           : CALIBRATION_VERIFY_MIN_TOLERANCE_G;
}

inline float calibrationStabilityRawLimit(float currentFactor) {
  float magnitude = fabsf(currentFactor);
  if (!isfinite(magnitude) || magnitude < CALIBRATION_VALUE_MIN_ABS) {
    magnitude = CALIBRATION_VALUE_DEFAULT;
  }
  float limit = magnitude * CALIBRATION_STABILITY_MAX_G;
  return limit > 50.0f ? limit : 50.0f;
}

inline CalibrationRejectReason validateCalibrationCandidateBasics(float knownMass,
                                                                  long rawDelta,
                                                                  float candidateFactor) {
  if (!isfinite(knownMass) || knownMass < CALIBRATION_KNOWN_MASS_MIN_G) {
    return CAL_REJECT_UNKNOWN_MASS;
  }
  if (fabsf((float)rawDelta) < knownMass * CALIBRATION_VALUE_MIN_ABS) {
    return CAL_REJECT_RAW_DELTA_TOO_SMALL;
  }
  if (!isfinite(candidateFactor)) {
    return CAL_REJECT_FACTOR_NEAR_ZERO;
  }
  float magnitude = fabsf(candidateFactor);
  if (magnitude < CALIBRATION_VALUE_MIN_ABS) {
    return CAL_REJECT_FACTOR_NEAR_ZERO;
  }
  if (magnitude > CALIBRATION_VALUE_MAX_ABS) {
    return CAL_REJECT_FACTOR_TOO_LARGE;
  }
#if CALIBRATION_ALLOW_NEGATIVE_FACTOR
  if ((candidateFactor > 0.0f && rawDelta <= 0) ||
      (candidateFactor < 0.0f && rawDelta >= 0)) {
    return CAL_REJECT_FACTOR_SIGN;
  }
#else
  if (candidateFactor <= 0.0f || rawDelta <= 0) {
    return CAL_REJECT_FACTOR_SIGN;
  }
#endif
  return CAL_REJECT_NONE;
}

inline CalibrationRejectReason validateCalibrationVerification(float knownMass,
                                                               float verifiedWeight) {
  if (!isfinite(verifiedWeight) || verifiedWeight <= 0.0f) {
    return CAL_REJECT_VERIFY_INVERTED;
  }
  if (fabsf(verifiedWeight - knownMass) > calibrationVerifyTolerance(knownMass)) {
    return CAL_REJECT_VERIFY_TOLERANCE;
  }
  return CAL_REJECT_NONE;
}

#endif
