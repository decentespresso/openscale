#include "fuel_gauge_internal.h"
#include "fuel_gauge.h"
#include <Wire.h>

static bool gaugeWriteByte(uint8_t reg, uint8_t value) {
  delayMicroseconds(70);
  Wire.beginTransmission(gaugeAddress);
  const bool queued = Wire.write(reg) == 1 && Wire.write(value) == 1;
  return Wire.endTransmission(true) == 0 && queued;
}

static bool gaugeReadBytes(uint8_t reg, uint8_t *bytes, uint8_t count) {
  delayMicroseconds(70);
  Wire.beginTransmission(gaugeAddress);
  const bool queued = Wire.write(reg) == 1;
  if (Wire.endTransmission(true) != 0 || !queued) return false;
  delayMicroseconds(70);
  if (Wire.requestFrom(gaugeAddress, count) != count || Wire.available() != count) {
    return false;
  }
  for (uint8_t index = 0; index < count; ++index) {
    const int value = Wire.read();
    if (value < 0) return false;
    bytes[index] = static_cast<uint8_t>(value);
  }
  return true;
}

bool gaugeReadWord(uint8_t reg, uint16_t &value) {
  uint8_t bytes[2];
  if (!gaugeReadBytes(reg, bytes, sizeof(bytes))) return false;
  value = bytes[0] | static_cast<uint16_t>(bytes[1]) << 8;
  return true;
}

static bool gaugeCommand(uint16_t command) {
  delay(500);
  return gaugeWriteByte(0x00, command & 0xff) &&
         gaugeWriteByte(0x01, command >> 8);
}

bool gaugeReadControl(uint16_t command, uint16_t &value) {
  if (!gaugeCommand(command)) return false;
  delay(2);
  return gaugeReadWord(0x00, value);
}

static bool gaugeWaitConfig(bool enabled) {
  const unsigned long startedAt = millis();
  while (millis() - startedAt < 3000) {
    delay(500);
    uint16_t flags;
    if (!gaugeReadWord(0x06, flags)) return false;
    if (((flags & gaugeConfigFlag) != 0) == enabled) return true;
  }
  return false;
}

static uint8_t gaugeChecksum(const uint8_t *bytes) {
  uint8_t sum = 0;
  for (uint8_t index = 0; index < 32; ++index) sum += bytes[index];
  return 255 - sum;
}

static bool gaugeSelectBlock(uint8_t subclass) {
  if (!gaugeWriteByte(0x61, 0) || !gaugeWriteByte(0x3e, subclass) ||
      !gaugeWriteByte(0x3f, 0)) return false;
  delay(10);
  return true;
}

static bool gaugeReadBlock(uint8_t subclass, uint8_t *bytes) {
  uint8_t checksum;
  return gaugeSelectBlock(subclass) && gaugeReadBytes(0x40, bytes, 32) &&
         gaugeReadBytes(0x60, &checksum, 1) && checksum == gaugeChecksum(bytes);
}

static bool gaugeWriteBlock(uint8_t subclass, const uint8_t *bytes) {
  if (!gaugeSelectBlock(subclass)) return false;
  for (uint8_t index = 0; index < 32; ++index) {
    if (!gaugeWriteByte(0x40 + index, bytes[index])) return false;
  }
  if (!gaugeWriteByte(0x60, gaugeChecksum(bytes))) return false;
  delay(20);
  uint8_t verified[32];
  if (!gaugeReadBlock(subclass, verified)) return false;
  for (uint8_t index = 0; index < 32; ++index) {
    if (verified[index] != bytes[index]) return false;
  }
  return true;
}

static bool gaugeApplyConfiguration(uint16_t &capacity, bool adoptCapacity,
                                    bool &configAttempted) {
  uint16_t flags;
  uint16_t chemistry;
  uint8_t state[32];
  uint8_t calibration[32];
  if (!gaugeReadWord(0x06, flags)) return false;
  configAttempted = (flags & gaugeConfigFlag) != 0;
  if (!gaugeReadControl(0x0008, chemistry) ||
      !gaugeReadBlock(82, state) || !gaugeReadBlock(105, calibration)) return false;
  const uint16_t actualCapacity = static_cast<uint16_t>(state[6]) << 8 | state[7];
  if (capacity == 0) {
    capacity = adoptCapacity && !(flags & gaugePorFlag) &&
               actualCapacity >= 300 && actualCapacity <= 2000 ? actualCapacity : 700;
  }
  const bool changeChemistry = chemistry != 0x1202;
  const bool changeCapacity = actualCapacity != capacity;
  const bool changeGain = (calibration[5] & 0x80) != 0;
  if (!changeChemistry && !changeCapacity && !changeGain &&
      !(flags & (gaugePorFlag | gaugeConfigFlag))) return true;

  configAttempted = true;
  if (!gaugeCommand(0x0013) || !gaugeWaitConfig(true)) return false;
  delay(1000);
  if (changeChemistry && !gaugeCommand(0x0031)) return false;
  if (changeCapacity) {
    if (!gaugeReadBlock(82, state)) return false;
    state[6] = capacity >> 8;
    state[7] = capacity & 0xff;
    if (!gaugeWriteBlock(82, state)) return false;
  }
  if (changeGain) {
    if (!gaugeReadBlock(105, calibration)) return false;
    calibration[5] &= 0x7f;
    if (!gaugeWriteBlock(105, calibration)) return false;
  }
  return true;
}

bool gaugeConfigure(uint16_t &capacity, bool adoptCapacity) {
  uint16_t status;
  if (!gaugeReadControl(0x0000, status)) return false;
  const bool wasSealed = (status & 0x2000) != 0 || fuelGaugeRuntime.sealRequired;
  fuelGaugeRuntime.sealRequired = wasSealed;
  bool configured = true;
  bool configAttempted = false;
  if (wasSealed) {
    configured = gaugeCommand(0x8000) && gaugeCommand(0x8000) &&
                 gaugeReadControl(0x0000, status) && !(status & 0x2000);
  }
  if (configured) {
    configured = gaugeApplyConfiguration(capacity, adoptCapacity, configAttempted);
  }
  bool exited = true;
  if (configAttempted) {
    exited = gaugeCommand(0x0042) && gaugeWaitConfig(false);
    delay(2000);
  }
  if (configured && exited) {
    uint16_t chemistry;
    uint16_t flags;
    uint8_t state[32];
    uint8_t calibration[32];
    configured = gaugeReadControl(0x0008, chemistry) && chemistry == 0x1202 &&
                 gaugeReadBlock(82, state) &&
                 (static_cast<uint16_t>(state[6]) << 8 | state[7]) == capacity &&
                 gaugeReadBlock(105, calibration) && !(calibration[5] & 0x80) &&
                 gaugeReadWord(0x06, flags) &&
                 !(flags & (gaugeConfigFlag | gaugePorFlag));
  }
  bool restoredSeal = true;
  if (wasSealed) {
    restoredSeal = gaugeCommand(0x0020) && gaugeReadControl(0x0000, status) &&
                   (status & 0x2000);
    if (restoredSeal) fuelGaugeRuntime.sealRequired = false;
  }
  return configured && exited && restoredSeal;
}
