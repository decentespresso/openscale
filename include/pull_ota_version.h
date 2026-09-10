#ifndef PULL_OTA_VERSION_H
#define PULL_OTA_VERSION_H

#define HDS_OTA_RELEASE_RECOVERY_VERSION 1
#define HDS_OTA_FORWARD_RECOVERY_VERSION 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct PullOtaVersionTriplet {
  uint16_t parts[3] = {0, 0, 0};
  const char *suffix = nullptr;
};

inline bool pullOtaParseVersionPrefix(const char *version, PullOtaVersionTriplet &parsed) {
  if (version == nullptr) {
    return false;
  }
  if (*version == 'v' || *version == 'V') {
    version++;
  }
  for (uint8_t part = 0; part < 3; part++) {
    if (*version < '0' || *version > '9') {
      return false;
    }
    uint32_t value = 0;
    while (*version >= '0' && *version <= '9') {
      value = value * 10 + (*version - '0');
      if (value > UINT16_MAX) {
        return false;
      }
      version++;
    }
    parsed.parts[part] = static_cast<uint16_t>(value);
    if (part < 2) {
      if (*version != '.') {
        return false;
      }
      version++;
    }
  }
  parsed.suffix = version;
  return true;
}

inline bool pullOtaVersionIsStable(const char *version) {
  PullOtaVersionTriplet parsed;
  return pullOtaParseVersionPrefix(version, parsed) && *parsed.suffix == '\0';
}

inline bool pullOtaVersionIsRelease(const char *version) {
  PullOtaVersionTriplet parsed;
  if (!pullOtaParseVersionPrefix(version, parsed)) return false;
  const char *suffix = parsed.suffix;
  if (*suffix == '\0') return true;
  if (strncmp(suffix, "-preview.", 9) == 0) suffix += 9;
  else if (strncmp(suffix, "-rc.", 4) == 0) suffix += 4;
  else return false;
  if (*suffix < '0' || *suffix > '9') return false;
  while (*suffix >= '0' && *suffix <= '9') suffix++;
  return *suffix == '\0';
}

inline bool pullOtaVersionHasComparablePrefix(const char *version, PullOtaVersionTriplet &parsed) {
  return pullOtaParseVersionPrefix(version, parsed) &&
         (*parsed.suffix == '\0' || *parsed.suffix == '-');
}

inline bool pullOtaNormalizeVersionPrefix(
    const char *version,
    char *normalized,
    size_t normalizedSize) {
  PullOtaVersionTriplet parsed;
  if (normalized == nullptr || normalizedSize == 0 ||
      !pullOtaVersionHasComparablePrefix(version, parsed)) {
    return false;
  }
  int length = snprintf(
      normalized, normalizedSize, "%u.%u.%u",
      parsed.parts[0], parsed.parts[1], parsed.parts[2]);
  return length > 0 && static_cast<size_t>(length) < normalizedSize;
}

inline int pullOtaCompareVersionPrefixes(const char *left, const char *right) {
  PullOtaVersionTriplet leftParsed;
  PullOtaVersionTriplet rightParsed;
  if (!pullOtaVersionHasComparablePrefix(left, leftParsed) ||
      !pullOtaVersionHasComparablePrefix(right, rightParsed)) {
    return 0;
  }
  for (uint8_t part = 0; part < 3; part++) {
    if (leftParsed.parts[part] < rightParsed.parts[part]) return -1;
    if (leftParsed.parts[part] > rightParsed.parts[part]) return 1;
  }
  return 0;
}

#endif
