#pragma once
#include <Arduino.h>

enum esp_sleep_wakeup_cause_t { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_EXT1 };
constexpr int ESP_EXT1_WAKEUP_ANY_LOW = 0;
inline esp_sleep_wakeup_cause_t fakeWakeCause = ESP_SLEEP_WAKEUP_TIMER;
inline uint64_t fakeWakeStatus = 0, fakeWakeMask = 0, fakeTimerUs = 0;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return fakeWakeCause; }
inline uint64_t esp_sleep_get_ext1_wakeup_status() { return fakeWakeStatus; }
inline void esp_sleep_disable_ext1_wakeup_io(uint64_t mask) { fakeWakeMask &= ~mask; }
inline void esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int) { fakeWakeMask |= mask; }
inline void esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t cause) {
  assert(cause == ESP_SLEEP_WAKEUP_TIMER);
  fakeTimerUs = 0;
}
inline void esp_sleep_enable_timer_wakeup(uint64_t interval) { fakeTimerUs = interval; }
struct FakeDeepSleep {};
inline void esp_deep_sleep_start() { throw FakeDeepSleep{}; }
