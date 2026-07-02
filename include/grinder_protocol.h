#ifndef GRINDER_PROTOCOL_H
#define GRINDER_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef GRINDER_TCP_PORT
#define GRINDER_TCP_PORT 31980
#endif

#ifndef GRINDER_TCP_MAX_LINE_LENGTH
#define GRINDER_TCP_MAX_LINE_LENGTH 96
#endif

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
  if (value == nullptr) {
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

static inline float grinderCutoffGrams(float targetGrams, float grindRateGps, float effectiveLatencySeconds, float safetyMarginGrams) {
  const float safeRate = grindRateGps > 0.0f ? grindRateGps : 0.0f;
  return targetGrams - (safeRate * effectiveLatencySeconds) - safetyMarginGrams;
}

#endif
