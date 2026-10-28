#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2;
inline uint64_t fakeMicros = 0;
inline std::array<int, 64> pinLevels{};
inline std::array<int, 64> pinModes{};
inline std::array<bool, 64> pinHolds{};
inline std::vector<int> pinWrites;
inline unsigned long buttonBackAt = std::numeric_limits<unsigned long>::max();
inline unsigned long millis() { return static_cast<unsigned long>(fakeMicros / 1000); }
inline void delay(unsigned long duration) { fakeMicros += duration * 1000; }
inline void delayMicroseconds(unsigned int duration) { fakeMicros += duration; }
inline void digitalWrite(int pin, int level) { pinLevels[pin] = level; pinWrites.push_back(pin); }
inline int digitalRead(int pin) { return pin == 1 && millis() >= buttonBackAt ? LOW : pinLevels[pin]; }
inline void pinMode(int pin, int mode) { pinModes[pin] = mode; }
struct FakeSerial {
  int writes = 0;
  template<class... T> void printf(const char *, T...) { ++writes; }
  void println(const char *) { ++writes; }
};
inline FakeSerial Serial;
