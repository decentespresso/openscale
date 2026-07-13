#ifndef GRINDER_PROTOCOL_H
#define GRINDER_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef GRINDER_TCP_PORT
#define GRINDER_TCP_PORT 31980
#endif

#ifndef GRINDER_TCP_MAX_LINE_LENGTH
#define GRINDER_TCP_MAX_LINE_LENGTH 96
#endif

#define GRINDER_TARGET_DEFAULT_GRAMS 15.0f
#define GRINDER_TARGET_MIN_GRAMS 10.0f
#define GRINDER_TARGET_MAX_GRAMS 200.0f
#define GRINDER_SAFETY_DEFAULT_GRAMS 2.0f
#define GRINDER_SAFETY_MAX_GRAMS 10.0f
#define GRINDER_MIN_CUTOFF_GRAMS 5.0f
#define GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500
#define GRINDER_MAX_GRIND_RATE_GPS 6.0f

enum GrinderTcpResponseKind {
  GRINDER_TCP_RESPONSE_INVALID,
  GRINDER_TCP_RESPONSE_OK,
  GRINDER_TCP_RESPONSE_BUSY,
  GRINDER_TCP_RESPONSE_ERR
};

struct GrinderTcpResponse {
  GrinderTcpResponseKind kind;
  char plugMac[18];
  bool relayOn;
  char reason[24];
};

static inline bool grinderIsUpperHex(char value) {
  return ((value >= '0') && (value <= '9')) || ((value >= 'A') && (value <= 'F'));
}

static inline bool grinderIsMac(const char *value) {
  if (value == nullptr || strlen(value) != 17) {
    return false;
  }
  for (uint8_t i = 0; i < 17; i++) {
    const bool separator = (i == 2) || (i == 5) || (i == 8) || (i == 11) || (i == 14);
    if (separator) {
      if (value[i] != ':') {
        return false;
      }
    } else if (!grinderIsUpperHex(value[i])) {
      return false;
    }
  }
  return true;
}

static inline void grinderClearResponse(GrinderTcpResponse *response) {
  response->kind = GRINDER_TCP_RESPONSE_INVALID;
  response->plugMac[0] = 0;
  response->relayOn = false;
  response->reason[0] = 0;
}

static inline bool grinderCopyReason(char *output, size_t outputSize, const char *input) {
  if (output == nullptr || outputSize == 0 || input == nullptr || input[0] == 0) {
    return false;
  }
  size_t inputLength = strlen(input);
  if (inputLength >= outputSize) {
    return false;
  }
  for (size_t i = 0; i < inputLength; i++) {
    const char c = input[i];
    if (c <= 32 || c > 126) {
      return false;
    }
  }
  memcpy(output, input, inputLength + 1);
  return true;
}

static inline bool grinderCopyMac(char *output, const char *input) {
  if (!grinderIsMac(input)) {
    return false;
  }
  memcpy(output, input, 18);
  return true;
}

static inline bool grinderIsMacPrefix(const char *value) {
  if (value == nullptr || strlen(value) < 17) {
    return false;
  }
  for (uint8_t i = 0; i < 17; i++) {
    const bool separator = (i == 2) || (i == 5) || (i == 8) || (i == 11) || (i == 14);
    if (separator) {
      if (value[i] != ':') {
        return false;
      }
    } else if (!grinderIsUpperHex(value[i])) {
      return false;
    }
  }
  return true;
}

static inline bool grinderCopyMacPrefix(char *output, const char *input) {
  if (!grinderIsMacPrefix(input)) {
    return false;
  }
  memcpy(output, input, 17);
  output[17] = 0;
  return true;
}

static inline bool grinderParseOkResponse(const char *line, GrinderTcpResponse *response) {
  if (line == nullptr || response == nullptr) {
    return false;
  }
  const size_t length = strlen(line);
  if (length != 29 && length != 30) {
    return false;
  }
  if (strncmp(line, "OK ", 3) != 0) {
    return false;
  }
  const char *mac = line + 3;
  if (!grinderCopyMacPrefix(response->plugMac, mac)) {
    return false;
  }
  const char *state = line + 20;
  if (strcmp(state, " state=OFF") == 0) {
    response->kind = GRINDER_TCP_RESPONSE_OK;
    response->relayOn = false;
    return true;
  }
  if (strcmp(state, " state=ON") == 0) {
    response->kind = GRINDER_TCP_RESPONSE_OK;
    response->relayOn = true;
    return true;
  }
  return false;
}

static inline bool grinderParseBusyResponse(const char *line, GrinderTcpResponse *response) {
  if (line == nullptr || response == nullptr) {
    return false;
  }
  if (strncmp(line, "BUSY ", 5) != 0) {
    return false;
  }
  if (strlen(line) != 22) {
    return false;
  }
  if (!grinderCopyMac(response->plugMac, line + 5)) {
    return false;
  }
  response->kind = GRINDER_TCP_RESPONSE_BUSY;
  response->relayOn = false;
  return true;
}

static inline bool grinderParseErrResponse(const char *line, GrinderTcpResponse *response) {
  if (line == nullptr || response == nullptr) {
    return false;
  }
  const size_t length = strlen(line);
  if (length < 30 || length > GRINDER_TCP_MAX_LINE_LENGTH) {
    return false;
  }
  if (strncmp(line, "ERR ", 4) != 0) {
    return false;
  }
  const char *mac = line + 4;
  if (!grinderCopyMacPrefix(response->plugMac, mac)) {
    return false;
  }
  const char *reason = line + 21;
  if (strncmp(reason, " reason=", 8) != 0) {
    return false;
  }
  if (!grinderCopyReason(response->reason, sizeof(response->reason), reason + 8)) {
    return false;
  }
  response->kind = GRINDER_TCP_RESPONSE_ERR;
  response->relayOn = false;
  return true;
}

static inline bool grinderParseResponse(const char *line, GrinderTcpResponse *response) {
  if (line == nullptr || response == nullptr) {
    return false;
  }
  grinderClearResponse(response);
  if (grinderParseOkResponse(line, response)) {
    return true;
  }
  grinderClearResponse(response);
  if (grinderParseBusyResponse(line, response)) {
    return true;
  }
  grinderClearResponse(response);
  if (grinderParseErrResponse(line, response)) {
    return true;
  }
  grinderClearResponse(response);
  return false;
}

static inline int grinderFormatHello(char *output, size_t outputSize, const char *scaleMac) {
  return snprintf(output, outputSize, "HELLO %s", scaleMac);
}

static inline float grinderCutoffGrams(float targetGrams, float safetyMarginGrams) {
  return targetGrams - safetyMarginGrams;
}

static inline float grinderNormalizeTargetGrams(float targetGrams) {
  if (!isfinite(targetGrams)) {
    return GRINDER_TARGET_DEFAULT_GRAMS;
  }
  if (targetGrams < GRINDER_TARGET_MIN_GRAMS) {
    return GRINDER_TARGET_MIN_GRAMS;
  }
  return targetGrams > GRINDER_TARGET_MAX_GRAMS ? GRINDER_TARGET_MAX_GRAMS : targetGrams;
}

static inline float grinderMaxSafetyGrams(float targetGrams) {
  const float targetLimit = grinderNormalizeTargetGrams(targetGrams) - GRINDER_MIN_CUTOFF_GRAMS;
  return targetLimit < GRINDER_SAFETY_MAX_GRAMS ? targetLimit : GRINDER_SAFETY_MAX_GRAMS;
}

static inline float grinderNormalizeSafetyGrams(float safetyMarginGrams, float targetGrams) {
  const float maxSafety = grinderMaxSafetyGrams(targetGrams);
  if (!isfinite(safetyMarginGrams)) {
    return GRINDER_SAFETY_DEFAULT_GRAMS < maxSafety ? GRINDER_SAFETY_DEFAULT_GRAMS : maxSafety;
  }
  if (safetyMarginGrams < 0.0f) {
    return 0.0f;
  }
  return safetyMarginGrams > maxSafety ? maxSafety : safetyMarginGrams;
}

static inline bool grinderCutoffShouldStop(float weight,
                                           float zeroMaxGrams,
                                           float targetGrams,
                                           float safetyMarginGrams,
                                           bool tarePending,
                                           uint32_t now,
                                           bool *guardActive,
                                           uint32_t *zeroExitAt,
                                           bool *setupMassBlocked) {
  if (guardActive == nullptr || zeroExitAt == nullptr || setupMassBlocked == nullptr) {
    return false;
  }
  if (weight <= zeroMaxGrams) {
    *guardActive = false;
    *zeroExitAt = 0;
    *setupMassBlocked = false;
    return false;
  }
  if (!*guardActive) {
    *guardActive = true;
    *zeroExitAt = now;
    if (weight >= grinderCutoffGrams(targetGrams, safetyMarginGrams)) {
      *setupMassBlocked = true;
    }
    return false;
  }
  if (*setupMassBlocked || tarePending || weight < grinderCutoffGrams(targetGrams, safetyMarginGrams)) {
    return false;
  }
  const uint32_t elapsed = now - *zeroExitAt;
  if (elapsed < GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS) {
    return false;
  }
  const float averageRate = (weight - zeroMaxGrams) / ((float)elapsed / 1000.0f);
  if (averageRate > GRINDER_MAX_GRIND_RATE_GPS) {
    *setupMassBlocked = true;
    return false;
  }
  return true;
}

static inline bool grinderCanArmAfterTare(bool userTareComplete, bool zeroStable) {
  return userTareComplete && zeroStable;
}

#endif
