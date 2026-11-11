#ifndef APP_PROMPT_SCROLL_H
#define APP_PROMPT_SCROLL_H

#include <stdint.h>

constexpr unsigned long APP_PROMPT_SCROLL_SPEED = 52;
constexpr unsigned long APP_PROMPT_SCROLL_HOLD_MS = 1000;

struct AppPromptScrollState {
  bool visible = false;
  uint32_t generation = 0;
  unsigned long startedAt = 0;
  int textWidth = 0;
};

inline AppPromptScrollState appPromptScrollUpdate(const AppPromptScrollState &state,
                                                 bool visible, uint32_t generation,
                                                 unsigned long now) {
  if (!visible) return {};
  if (!state.visible || state.generation != generation) return {true, generation, now};
  return state;
}

inline unsigned long appPromptScrollTravelMs(unsigned long distance) {
  return (distance * 1000UL + APP_PROMPT_SCROLL_SPEED - 1) / APP_PROMPT_SCROLL_SPEED;
}

inline int appPromptTextX(const AppPromptScrollState &state, int textWidth,
                          int viewportWidth, unsigned long now) {
  if (textWidth <= viewportWidth) return (viewportWidth - textWidth) / 2;
  const unsigned long distance = static_cast<unsigned long>(textWidth - viewportWidth);
  const unsigned long elapsed = static_cast<uint32_t>(now - state.startedAt);
  const unsigned long phase = elapsed % (2 * APP_PROMPT_SCROLL_HOLD_MS + appPromptScrollTravelMs(distance));
  if (phase <= APP_PROMPT_SCROLL_HOLD_MS) return 0;
  const unsigned long offset = (phase - APP_PROMPT_SCROLL_HOLD_MS) * APP_PROMPT_SCROLL_SPEED / 1000UL;
  return -static_cast<int>(offset < distance ? offset : distance);
}

inline unsigned long appPromptScrollWaitMs(const AppPromptScrollState &state, int textWidth,
                                          int viewportWidth, unsigned long now) {
  if (!state.visible || textWidth <= viewportWidth) return UINT32_MAX;
  const unsigned long distance = static_cast<unsigned long>(textWidth - viewportWidth);
  const unsigned long travelMs = appPromptScrollTravelMs(distance);
  const unsigned long cycleMs = 2 * APP_PROMPT_SCROLL_HOLD_MS + travelMs;
  const unsigned long elapsed = static_cast<uint32_t>(now - state.startedAt);
  const unsigned long phase = elapsed % cycleMs;
  if (phase >= APP_PROMPT_SCROLL_HOLD_MS + travelMs) return cycleMs - phase;
  const unsigned long offset = phase > APP_PROMPT_SCROLL_HOLD_MS
    ? (phase - APP_PROMPT_SCROLL_HOLD_MS) * APP_PROMPT_SCROLL_SPEED / 1000UL : 0;
  return APP_PROMPT_SCROLL_HOLD_MS + appPromptScrollTravelMs(offset + 1) - phase;
}

#endif
