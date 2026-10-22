#ifndef APP_PROMPT_STATE_H
#define APP_PROMPT_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t APP_PROMPT_MAX_BYTES = 18;
static constexpr uint16_t APP_PROMPT_NO_CONNECTION = UINT16_MAX;
static constexpr unsigned long APP_PROMPT_TIMEOUT_MS = 10000;

struct AppPromptState {
  uint16_t connectionHandle = APP_PROMPT_NO_CONNECTION;
  bool cancelled = false;
  bool active = false;
  unsigned long heartbeatAt = 0;
  char text[APP_PROMPT_MAX_BYTES + 1] = {};
};

inline AppPromptState appPromptConnection(uint16_t handle, unsigned long now) {
  AppPromptState state;
  state.connectionHandle = handle;
  state.heartbeatAt = now;
  return state;
}

inline AppPromptState appPromptExpire(const AppPromptState &state, unsigned long now) {
  AppPromptState next = state;
  if (state.connectionHandle == APP_PROMPT_NO_CONNECTION || now - state.heartbeatAt >= APP_PROMPT_TIMEOUT_MS) {
    next.active = false;
    memset(next.text, 0, sizeof(next.text));
  }
  return next;
}

inline AppPromptState appPromptHeartbeat(const AppPromptState &state, unsigned long now) {
  AppPromptState next = appPromptExpire(state, now);
  if (state.connectionHandle != APP_PROMPT_NO_CONNECTION) next.heartbeatAt = now;
  return next;
}

inline AppPromptState appPromptCancel(const AppPromptState &state) {
  AppPromptState next = state;
  next.cancelled = true;
  next.active = false;
  memset(next.text, 0, sizeof(next.text));
  return next;
}

inline AppPromptState appPromptCommand(const AppPromptState &state, const uint8_t *bytes,
                                      size_t length, unsigned long now) {
  AppPromptState next = appPromptExpire(state, now);
  if (bytes == nullptr || length == 0 || state.connectionHandle == APP_PROMPT_NO_CONNECTION || state.cancelled) return next;
  if (length == 1 && bytes[0] == 0) {
    next.active = false;
    memset(next.text, 0, sizeof(next.text));
    return next;
  }
  if (bytes[0] != 1 || length < 2 || length > APP_PROMPT_MAX_BYTES + 1 ||
      now - state.heartbeatAt >= APP_PROMPT_TIMEOUT_MS) return next;
  for (size_t index = 1; index < length; ++index) {
    if (bytes[index] < 0x20 || bytes[index] > 0x7e) return next;
  }
  memset(next.text, 0, sizeof(next.text));
  memcpy(next.text, bytes + 1, length - 1);
  next.active = true;
  return next;
}

inline bool appPromptIsHeartbeat(const uint8_t *bytes, size_t length) {
  const uint8_t heartbeat[] = {0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a};
  return bytes != nullptr && length == sizeof(heartbeat) && memcmp(bytes, heartbeat, sizeof(heartbeat)) == 0;
}

#endif
