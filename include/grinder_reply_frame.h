#ifndef GRINDER_REPLY_FRAME_H
#define GRINDER_REPLY_FRAME_H

#include "grinder_protocol.h"

enum class GrinderReplyResult { Ignored, Partial, Complete, Invalid };

struct GrinderReplyFrame {
  char line[GRINDER_TCP_MAX_LINE_LENGTH + 1] = {};
  size_t length = 0;
  uint8_t requestId = 0;
  bool pendingCr = false;

  GrinderReplyResult append(uint8_t expectedId, uint8_t receivedId, const char *data, size_t size) {
    if (receivedId != 0 && receivedId != expectedId) return GrinderReplyResult::Ignored;
    if (receivedId != requestId) {
      length = 0;
      pendingCr = false;
      requestId = receivedId;
    }
    for (size_t i = 0; i < size; ++i) {
      const char value = data[i];
      if (value == '\n') {
        if (length == 0 || i + 1 != size) return GrinderReplyResult::Invalid;
        line[length] = 0;
        length = 0;
        pendingCr = false;
        return GrinderReplyResult::Complete;
      }
      if (pendingCr || length == GRINDER_TCP_MAX_LINE_LENGTH) return GrinderReplyResult::Invalid;
      if (value == '\r') pendingCr = true;
      else if (value >= 32 && value <= 126) line[length++] = value;
      else return GrinderReplyResult::Invalid;
    }
    return GrinderReplyResult::Partial;
  }
};

#endif
