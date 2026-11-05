#include <cassert>
#include <cstdint>
#include "app_prompt_scroll.h"

void testScrollCycle() {
  const auto frame = appPromptScrollUpdate({}, true, 1, 100);
  assert(frame.visible && frame.startedAt == 100);
  assert(appPromptTextX(frame, 100, 128, 9999) == 14);
  assert(appPromptTextX(frame, 128, 128, 9999) == 0);
  assert(appPromptTextX(frame, 140, 128, 100) == 0);
  assert(appPromptTextX(frame, 140, 128, 1099) == 0);
  assert(appPromptTextX(frame, 140, 128, 1100) == 0);
  assert(appPromptTextX(frame, 140, 128, 1200) == -1);
  assert(appPromptTextX(frame, 140, 128, 2200) == -11);
  assert(appPromptTextX(frame, 140, 128, 2300) == -12);
  assert(appPromptTextX(frame, 140, 128, 3299) == -12);
  assert(appPromptTextX(frame, 140, 128, 3300) == 0);
  assert(appPromptTextX(frame, 140, 128, 4300) == 0);
  assert(appPromptTextX(frame, 140, 128, 4400) == -1);
  assert(appPromptTextX(frame, 129, 128, 1200) == -1);
  assert(appPromptTextX(frame, 129, 128, 2199) == -1);
  assert(appPromptTextX(frame, 129, 128, 2200) == 0);
}

void testVisibilityAndContentChanges() {
  const auto initial = appPromptScrollUpdate({}, true, 5, 100);
  const auto repeated = appPromptScrollUpdate(initial, true, 5, 5000);
  assert(repeated.startedAt == initial.startedAt);
  const auto changed = appPromptScrollUpdate(repeated, true, 6, 5000);
  assert(changed.startedAt == 5000);
  assert(appPromptTextX(changed, 768, 128, 5000) == 0);
  const auto hidden = appPromptScrollUpdate(changed, false, 6, 6000);
  assert(!hidden.visible);
  const auto shown = appPromptScrollUpdate(hidden, true, 6, 9000);
  assert(shown.startedAt == 9000);
  assert(appPromptTextX(shown, 768, 128, 9000) == 0);
  assert(initial.visible && initial.startedAt == 100);
}

void testMillisecondRollover() {
  const unsigned long start = UINT32_MAX - 500UL;
  const auto frame = appPromptScrollUpdate({}, true, 1, start);
  const unsigned long later = static_cast<uint32_t>(start + 1500UL);
  assert(appPromptTextX(frame, 140, 128, later) == -5);
  const unsigned long restart = static_cast<uint32_t>(start + 3200UL);
  assert(appPromptTextX(frame, 140, 128, restart) == 0);
}

int main() {
  testScrollCycle();
  testVisibilityAndContentChanges();
  testMillisecondRollover();
}
