#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "app_prompt_scroll.h"

void testScrollCycle() {
  const auto frame = appPromptScrollUpdate({}, true, 1, 100);
  assert(frame.visible && frame.startedAt == 100);
  assert(appPromptTextX(frame, 100, 128, 9999) == 14);
  assert(appPromptTextX(frame, 128, 128, 9999) == 0);
  assert(appPromptTextX(frame, 140, 128, 100) == 0);
  assert(appPromptTextX(frame, 140, 128, 1099) == 0);
  assert(appPromptTextX(frame, 140, 128, 1100) == 0);
  assert(appPromptTextX(frame, 140, 128, 1119) == 0);
  assert(appPromptTextX(frame, 140, 128, 1120) == -1);
  assert(appPromptTextX(frame, 140, 128, 1138) == -1);
  assert(appPromptTextX(frame, 140, 128, 1139) == -2);
  assert(appPromptTextX(frame, 140, 128, 1330) == -11);
  assert(appPromptTextX(frame, 140, 128, 1331) == -12);
  assert(appPromptTextX(frame, 140, 128, 2330) == -12);
  assert(appPromptTextX(frame, 140, 128, 2331) == 0);
  assert(appPromptTextX(frame, 140, 128, 3331) == 0);
  assert(appPromptTextX(frame, 140, 128, 3351) == -1);
  assert(appPromptTextX(frame, 129, 128, 1119) == 0);
  assert(appPromptTextX(frame, 129, 128, 1120) == -1);
  assert(appPromptTextX(frame, 129, 128, 2119) == -1);
  assert(appPromptTextX(frame, 129, 128, 2120) == 0);
  assert(appPromptScrollTravelMs(76) == 1462);
  assert(appPromptTextX(frame, 204, 128, 3562) == 0);
  assert(appPromptScrollWaitMs({}, 204, 128, 100) == UINT32_MAX);
  assert(appPromptScrollWaitMs(frame, 100, 128, 100) == UINT32_MAX);
  assert(appPromptScrollWaitMs(frame, 128, 128, 100) == UINT32_MAX);
}

void testPixelDeadlines() {
  const auto frame = appPromptScrollUpdate({}, true, 1, 0);
  for (int distance : {1, 12, 76, 640}) {
    const unsigned long travelMs = appPromptScrollTravelMs(distance);
    const unsigned long cycleMs = 2000 + travelMs;
    int previousX = 0;
    for (unsigned long now = 0; now < cycleMs * 2; ++now) {
      const int x = appPromptTextX(frame, 128 + distance, 128, now);
      const unsigned long phase = now % cycleMs;
      if (phase < 1000) assert(x == 0);
      if (phase >= 1000 + travelMs) assert(x == -distance);
      if (phase != 0) assert(x == previousX || x == previousX - 1);
      const unsigned long waitMs = appPromptScrollWaitMs(frame, 128 + distance, 128, now);
      assert(waitMs > 0);
      assert(appPromptTextX(frame, 128 + distance, 128, now + waitMs - 1) == x);
      assert(appPromptTextX(frame, 128 + distance, 128, now + waitMs) != x);
      previousX = x;
    }
    if (distance >= 52) assert(appPromptTextX(frame, 128 + distance, 128, 2000) == -52);
  }
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
  assert(appPromptTextX(frame, 204, 128, later) == -26);
  assert(appPromptScrollWaitMs(frame, 204, 128, later) == 20);
  const unsigned long restart = static_cast<uint32_t>(start + 3462UL);
  assert(appPromptTextX(frame, 204, 128, restart) == 0);
  assert(appPromptScrollWaitMs(frame, 204, 128, restart) == 1020);
}

int main() {
  testScrollCycle();
  testPixelDeadlines();
  testVisibilityAndContentChanges();
  testMillisecondRollover();
}
