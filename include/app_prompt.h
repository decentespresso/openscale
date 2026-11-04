#ifndef APP_PROMPT_H
#define APP_PROMPT_H

#include "app_prompt_state.h"

inline bool appTextIsEnabled() {
  portENTER_CRITICAL(&appPromptMux);
  const bool enabled = appTextEnabled;
  portEXIT_CRITICAL(&appPromptMux);
  return enabled;
}

inline AppPromptState readAppPromptLocked() {
  AppPromptState state;
  state.connectionHandle = appPromptState.connectionHandle;
  state.cancelled = appPromptState.cancelled;
  state.active = appPromptState.active;
  state.heartbeatAt = appPromptState.heartbeatAt;
  for (size_t index = 0; index < sizeof(state.text); ++index) state.text[index] = appPromptState.text[index];
  return state;
}

inline void storeAppPromptLocked(const AppPromptState &state) {
  appPromptState.connectionHandle = state.connectionHandle;
  appPromptState.cancelled = state.cancelled;
  appPromptState.active = state.active;
  appPromptState.heartbeatAt = state.heartbeatAt;
  for (size_t index = 0; index < sizeof(state.text); ++index) appPromptState.text[index] = state.text[index];
}

inline void setAppTextEnabled(bool enabled) {
  portENTER_CRITICAL(&appPromptMux);
  appTextEnabled = enabled;
  AppPromptState state = readAppPromptLocked();
  state.active = false;
  memset(state.text, 0, sizeof(state.text));
  storeAppPromptLocked(state);
  portEXIT_CRITICAL(&appPromptMux);
}

inline bool saveAppTextEnabled(bool enabled) {
  if (!storagePutBool(KEY_APP_TEXT, enabled)) return false;
  setAppTextEnabled(enabled);
  return true;
}

inline void setAppPromptConnection(uint16_t handle) {
  portENTER_CRITICAL(&appPromptMux);
  const unsigned long now = millis();
  storeAppPromptLocked(appPromptConnection(handle, now));
  portEXIT_CRITICAL(&appPromptMux);
}

inline void syncAppPromptConnection(uint16_t handle) {
  portENTER_CRITICAL(&appPromptMux);
  const unsigned long now = millis();
  if (appPromptState.connectionHandle != handle) storeAppPromptLocked(appPromptConnection(handle, now));
  portEXIT_CRITICAL(&appPromptMux);
}

inline void recordAppPromptHeartbeat(const uint8_t *bytes, size_t length) {
  if (!appPromptIsHeartbeat(bytes, length)) return;
  portENTER_CRITICAL(&appPromptMux);
  const unsigned long now = millis();
  storeAppPromptLocked(appPromptHeartbeat(readAppPromptLocked(), now));
  portEXIT_CRITICAL(&appPromptMux);
}

inline void processAppPromptSession() {
  portENTER_CRITICAL(&appPromptMux);
  const unsigned long now = millis();
  const AppPromptState state = readAppPromptLocked();
  storeAppPromptLocked(b_ota || !b_ble_enabled
    ? appPromptConnection(APP_PROMPT_NO_CONNECTION, now)
    : appPromptExpire(state, now));
  portEXIT_CRITICAL(&appPromptMux);
}

inline AppPromptState snapshotAppPrompt() {
  portENTER_CRITICAL(&appPromptMux);
  const AppPromptState state = readAppPromptLocked();
  portEXIT_CRITICAL(&appPromptMux);
  return state;
}

inline bool cancelAppPrompt() {
  portENTER_CRITICAL(&appPromptMux);
  const unsigned long now = millis();
  const AppPromptState state = appPromptExpire(readAppPromptLocked(), now);
  const bool cancelled = state.active;
  if (cancelled) storeAppPromptLocked(appPromptCancel(state));
  portEXIT_CRITICAL(&appPromptMux);
  return cancelled;
}

#endif
