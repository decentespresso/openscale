#pragma once
#include <Arduino.h>
#include <algorithm>
#include <map>

struct FakeWire {
  std::array<std::array<uint8_t, 32>, 256> blocks{};
  std::map<uint8_t, uint16_t> words;
  std::map<uint8_t, std::vector<unsigned long>> reads;
  std::vector<uint8_t> tx, rx;
  uint8_t reg = 0, subclass = 0, commandLow = 0;
  uint16_t command = 0, chemistry = 0x1202, deviceType = 0x0427;
  bool present = true, sealed = true, configTimeout = false, checksumBad = false;
  int operations = 0, failAt = -1, shortReg = -1, negativeReg = -1, nackReg = -1;
  int unsealKeys = 0, configEntries = 0, resets = 0, seals = 0;

  FakeWire() {
    words = {{0x04, 3800}, {0x06, 8}, {0x1c, 50}, {0x1e, 2981},
             {0x10, 100}, {0x18, 380}, {0x0c, 350}, {0x0e, 700}, {0x20, 0x0164}};
    setCapacity(700);
  }
  void setCapacity(uint16_t capacity) { blocks[82][6] = capacity >> 8; blocks[82][7] = capacity & 0xff; }
  uint16_t capacity() const { return static_cast<uint16_t>(blocks[82][6]) << 8 | blocks[82][7]; }
  void begin(int, int) {}
  void beginTransmission(uint8_t address) { assert(address == 0x55); tx.clear(); }
  size_t write(uint8_t byte) { tx.push_back(byte); return 1; }
  bool fails() { return ++operations == failAt; }
  void control(uint16_t value) {
    command = value;
    if (value == 0x8000) {
      if (++unsealKeys == 2) { sealed = false; unsealKeys = 0; }
    } else {
      unsealKeys = 0;
    }
    if (value == 0x0013 && !sealed) {
      ++configEntries;
      if (!configTimeout) words[0x06] |= 0x10;
    }
    if (value == 0x0031 && !sealed && (words[0x06] & 0x10)) chemistry = 0x1202;
    if (value == 0x0042 && !sealed) { ++resets; words[0x06] &= ~0x30; }
    if (value == 0x0020) { ++seals; sealed = true; }
  }
  uint8_t endTransmission(bool = true) {
    if (fails() || !present) return 2;
    if (tx.empty()) return 0;
    reg = tx[0];
    if (reg == nackReg) return 2;
    if (tx.size() == 1) return 0;
    assert(tx.size() == 2);
    const uint8_t value = tx[1];
    if (reg == 0) commandLow = value;
    if (reg == 1) control(commandLow | static_cast<uint16_t>(value) << 8);
    if (reg == 0x3e) subclass = value;
    if (reg >= 0x40 && reg <= 0x5f) {
      if (sealed || !(words[0x06] & 0x10)) return 2;
      blocks[subclass][reg - 0x40] = value;
    }
    return 0;
  }
  uint8_t requestFrom(uint8_t address, uint8_t count) {
    assert(address == 0x55);
    rx.clear();
    reads[reg].push_back(millis());
    if (fails() || !present) return 0;
    if (reg >= 0x40 && reg <= 0x5f) {
      if (sealed) return 0;
      for (uint8_t index = 0; index < count; ++index) rx.push_back(blocks[subclass][reg - 0x40 + index]);
    } else if (reg == 0x60) {
      uint8_t sum = 0;
      for (uint8_t byte : blocks[subclass]) sum += byte;
      rx.push_back(static_cast<uint8_t>(255 - sum + (checksumBad ? 1 : 0)));
    } else {
      const uint16_t value = reg != 0 ? words.at(reg) : command == 0 ? (sealed ? 0x2000 : 0) :
                             command == 1 ? deviceType : command == 8 ? chemistry : 0;
      rx = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    }
    if (reg == shortReg && !rx.empty()) rx.pop_back();
    return static_cast<uint8_t>(rx.size());
  }
  int available() const { return static_cast<int>(rx.size()); }
  int read() {
    if (reg == negativeReg || rx.empty()) return -1;
    const int value = rx.front();
    rx.erase(rx.begin());
    return value;
  }
};
inline FakeWire Wire;
