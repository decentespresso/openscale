#ifndef PULL_OTA_TARGET_H
#define PULL_OTA_TARGET_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t HDS_OTA_TARGET_MAX_COMPONENT = 127;
static const uint8_t HDS_OTA_TARGET_BYTE_BIAS = 0x80;
static const size_t HDS_OTA_TARGET_PAYLOAD_BYTES = 3;
static const size_t HDS_OTA_TARGET_VERSION_BUFFER_BYTES = 14;

struct PullOtaTargetVersion {
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
  bool present = false;
};

inline PullOtaTargetVersion pullOtaNoTargetVersion() {
  return PullOtaTargetVersion();
}

inline PullOtaTargetVersion pullOtaMakeTargetVersion(
    uint8_t major, uint8_t minor, uint8_t patch) {
  PullOtaTargetVersion target;
  target.major = major;
  target.minor = minor;
  target.patch = patch;
  target.present = true;
  return target;
}

inline bool pullOtaTargetByteIsBiased(uint8_t value) {
  return (value & HDS_OTA_TARGET_BYTE_BIAS) != 0;
}

inline uint8_t pullOtaEncodeTargetByte(uint8_t value) {
  return (uint8_t)(HDS_OTA_TARGET_BYTE_BIAS | (value & HDS_OTA_TARGET_MAX_COMPONENT));
}

inline PullOtaTargetVersion pullOtaTargetFromBiasedBytes(const uint8_t *bytes) {
  if (bytes == nullptr) {
    return pullOtaNoTargetVersion();
  }
  return pullOtaMakeTargetVersion(
      (uint8_t)(bytes[0] & HDS_OTA_TARGET_MAX_COMPONENT),
      (uint8_t)(bytes[1] & HDS_OTA_TARGET_MAX_COMPONENT),
      (uint8_t)(bytes[2] & HDS_OTA_TARGET_MAX_COMPONENT));
}

inline bool pullOtaFormatTargetVersion(
    const PullOtaTargetVersion &target, char *out, size_t outSize) {
  if (!target.present || out == nullptr || outSize == 0) {
    return false;
  }
  int length = snprintf(
      out, outSize, "%u.%u.%u",
      (unsigned)target.major, (unsigned)target.minor, (unsigned)target.patch);
  return length > 0 && (size_t)length < outSize;
}

inline bool pullOtaParseTargetVersion(
    const char *text, size_t len, PullOtaTargetVersion &target) {
  if (text == nullptr) {
    return false;
  }
  size_t at = 0;
  size_t end = len;
  while (at < end && (unsigned char)text[at] <= ' ') {
    at++;
  }
  while (end > at && (unsigned char)text[end - 1] <= ' ') {
    end--;
  }
  if (at < end && (text[at] == 'v' || text[at] == 'V')) {
    at++;
  }
  uint8_t parts[3] = {0, 0, 0};
  for (uint8_t part = 0; part < 3; part++) {
    if (at >= end || text[at] < '0' || text[at] > '9') {
      return false;
    }
    uint16_t value = 0;
    while (at < end && text[at] >= '0' && text[at] <= '9') {
      value = (uint16_t)(value * 10 + (uint16_t)(text[at] - '0'));
      if (value > HDS_OTA_TARGET_MAX_COMPONENT) {
        return false;
      }
      at++;
    }
    parts[part] = (uint8_t)value;
    if (part < 2) {
      if (at >= end || text[at] != '.') {
        return false;
      }
      at++;
    }
  }
  if (at != end) {
    return false;
  }
  target = pullOtaMakeTargetVersion(parts[0], parts[1], parts[2]);
  return true;
}

#endif
