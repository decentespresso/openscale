#ifndef DECENT_PROTOCOL_FRAME_H
#define DECENT_PROTOCOL_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "pull_ota_target.h"

static inline uint8_t decentPayloadChecksum(const uint8_t *data, size_t len) {
  uint8_t xorSum = 0;
  for (size_t i = 0; i < len; i++) {
    xorSum ^= data[i];
  }
  return xorSum;
}

static inline bool decentHasChecksummedFrame(const uint8_t *data, size_t len, size_t frameLen) {
  if (len < frameLen || frameLen < 2) {
    return false;
  }
  return data[frameLen - 1] == decentPayloadChecksum(data, frameLen - 1);
}

static inline size_t decentFixedFrameLength(size_t len, size_t frameLen) {
  return len >= frameLen ? frameLen : 0;
}

static inline size_t decentChecksummedOrShortFrameLength(
    const uint8_t *data,
    size_t len,
    size_t shortLen,
    size_t frameLen,
    bool allowShort) {
  if (decentHasChecksummedFrame(data, len, frameLen)) {
    return frameLen;
  }
  if (len < shortLen) {
    return 0;
  }
  if (allowShort || len >= frameLen) {
    return shortLen;
  }
  return 0;
}

static inline size_t decentCommandFrameLength(const uint8_t *data, size_t len, bool allowShort) {
  if (data == nullptr || len == 0) {
    return 0;
  }
  if (data[0] != 0x03) {
    return 1;
  }
  if (len < 2) {
    return 0;
  }

  switch (data[1]) {
    case 0x0F:
      return decentFixedFrameLength(len, 7);
    case 0x0A:
      if (decentHasChecksummedFrame(data, len, 7)) {
        return 7;
      }
      if (len < 3) {
        return 0;
      }
      if (data[2] == 0x01) {
        return decentChecksummedOrShortFrameLength(data, len, 6, 7, allowShort);
      }
      if (data[2] == 0x03 || data[2] == 0x04) {
        return decentChecksummedOrShortFrameLength(data, len, 4, 7, allowShort);
      }
      return decentChecksummedOrShortFrameLength(data, len, 3, 7, allowShort);
    case 0x0B:
      return decentChecksummedOrShortFrameLength(data, len, 3, 7, allowShort);
    case 0x1A:
      return decentFixedFrameLength(len, 3);
    case 0x1B:
      if (len >= 3 && pullOtaTargetByteIsBiased(data[2])) {
        return decentFixedFrameLength(len, 2 + HDS_OTA_TARGET_PAYLOAD_BYTES);
      }
      return decentFixedFrameLength(len, 2);
#ifdef BUZZER
    case 0x1C:
      return decentFixedFrameLength(len, 3);
#endif
    case 0x1D:
      return decentFixedFrameLength(len, 3);
    case 0x1E:
      return decentFixedFrameLength(len, 4);
    case 0x1F:
      return decentFixedFrameLength(len, 2);
    case 0x20:
      if (len < 3) {
        return 0;
      }
      if (data[2] != 0x01) {
        return 3;
      }
      if (len >= 4) {
        return 4;
      }
      return allowShort ? 3 : 0;
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 0x21:
      return decentFixedFrameLength(len, 2);
#endif
    case 0x22:
      return decentFixedFrameLength(len, 2);
    case 0x25:
      return decentChecksummedOrShortFrameLength(data, len, 3, 4, allowShort);
    case 0x26:
      return decentChecksummedOrShortFrameLength(data, len, 3, 4, allowShort);
    default:
      return decentFixedFrameLength(len, 2);
  }
}

#endif
