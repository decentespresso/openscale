#ifndef APP_PROMPT_SCROLL_H
#define APP_PROMPT_SCROLL_H

#include <stdint.h>

struct AppPromptScrollState {
  bool visible = false;
  uint32_t generation = 0;
  unsigned long startedAt = 0;
};

inline AppPromptScrollState appPromptScrollUpdate(const AppPromptScrollState &state,
                                                 bool visible, uint32_t generation,
                                                 unsigned long now) {
  if (!visible) return {};
  if (!state.visible || state.generation != generation) return {true, generation, now};
  return state;
}

inline int appPromptTextX(const AppPromptScrollState &state, int textWidth,
                          int viewportWidth, unsigned long now) {
  if (textWidth <= viewportWidth) return (viewportWidth - textWidth) / 2;
  const unsigned long distance = static_cast<unsigned long>(textWidth - viewportWidth);
  const unsigned long elapsed = static_cast<uint32_t>(now - state.startedAt);
  const unsigned long phase = elapsed % (2000UL + distance * 100UL);
  if (phase <= 1000UL) return 0;
  const unsigned long offset = (phase - 1000UL) / 100UL;
  return -static_cast<int>(offset < distance ? offset : distance);
}

#endif
