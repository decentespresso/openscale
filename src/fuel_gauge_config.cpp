#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include <Wire.h>
#include <cstring>

static bool gaugeWrite(uint8_t reg, uint8_t value) {
  delayMicroseconds(70);
  Wire.beginTransmission(gaugeAddress);
  const bool queued = Wire.write(reg) == 1 && Wire.write(value) == 1;
  return Wire.endTransmission(true) == 0 && queued;
}

static bool gaugePoint(uint8_t reg) {
  delayMicroseconds(70);
  Wire.beginTransmission(gaugeAddress);
  const bool queued = Wire.write(reg) == 1;
  return Wire.endTransmission(true) == 0 && queued;
}

static bool gaugeReceive(uint8_t *bytes, uint8_t count) {
  delayMicroseconds(70);
  if (Wire.requestFrom(gaugeAddress, count) != count || Wire.available() != count) return false;
  for (uint8_t index = 0; index < count; ++index) {
    const int value = Wire.read();
    if (value < 0) return false;
    bytes[index] = static_cast<uint8_t>(value);
  }
  return true;
}

static uint8_t gaugeChecksum(const uint8_t *bytes) {
  uint8_t sum = 0;
  for (uint8_t index = 0; index < 32; ++index) sum += bytes[index];
  return 255 - sum;
}

static int gaugeIoFinish(bool success) {
  fuelGaugeRuntime.engine.io.kind = GaugeIoKind::None;
  return success ? 1 : -1;
}

static int gaugeWordStep(GaugeIo &io) {
  if (io.phase++ == 0) return gaugePoint(io.reg) ? 0 : gaugeIoFinish(false);
  uint8_t bytes[2];
  if (!gaugeReceive(bytes, 2)) return gaugeIoFinish(false);
  io.word = bytes[0] | static_cast<uint16_t>(bytes[1]) << 8;
  return gaugeIoFinish(true);
}

static int gaugeCommandStep(GaugeIo &io) {
  if (io.phase == 0) {
    if (millis() - io.startedAt < 500) return 0;
    io.phase = 1;
  }
  if (io.phase == 1) {
    if (!gaugeWrite(0, io.command & 0xff)) return gaugeIoFinish(false);
    io.phase = 2;
    return 0;
  }
  if (io.phase == 2) {
    if (!gaugeWrite(1, io.command >> 8)) return gaugeIoFinish(false);
    io.startedAt = millis();
    io.phase = 3;
    return io.kind == GaugeIoKind::Command ? gaugeIoFinish(true) : 0;
  }
  if (io.phase == 3) {
    if (millis() - io.startedAt < 2) return 0;
    io.phase = 4;
    return gaugePoint(0) ? 0 : gaugeIoFinish(false);
  }
  uint8_t bytes[2];
  if (!gaugeReceive(bytes, 2)) return gaugeIoFinish(false);
  io.word = bytes[0] | static_cast<uint16_t>(bytes[1]) << 8;
  return gaugeIoFinish(true);
}

static int gaugeReadBlockStep(GaugeIo &io, uint8_t *bytes) {
  switch (io.phase) {
    case 0:
      if (!gaugeWrite(0x61, 0)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 1:
      if (!gaugeWrite(0x3e, io.subclass)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 2:
      if (!gaugeWrite(0x3f, 0)) return gaugeIoFinish(false);
      io.startedAt = millis();
      ++io.phase;
      return 0;
    case 3:
      if (millis() - io.startedAt < 10) return 0;
      if (!gaugePoint(0x40)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 4:
      if (!gaugeReceive(bytes, 32)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 5:
      if (!gaugePoint(0x60)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    default:
      uint8_t checksum;
      return gaugeIoFinish(gaugeReceive(&checksum, 1) && checksum == gaugeChecksum(bytes));
  }
}

static int gaugeWriteBlockStep(GaugeIo &io) {
  switch (io.phase) {
    case 0:
      if (!gaugeWrite(0x61, 0)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 1:
      if (!gaugeWrite(0x3e, io.subclass)) return gaugeIoFinish(false);
      ++io.phase;
      return 0;
    case 2:
      if (!gaugeWrite(0x3f, 0)) return gaugeIoFinish(false);
      io.startedAt = millis();
      ++io.phase;
      return 0;
    case 3:
      if (millis() - io.startedAt < 10) return 0;
      io.phase = 4;
      return 0;
    case 4:
      if (!gaugeWrite(0x40 + io.index, io.bytes[io.index])) return gaugeIoFinish(false);
      if (++io.index == 32) ++io.phase;
      return 0;
    case 5:
      if (!gaugeWrite(0x60, gaugeChecksum(io.bytes))) return gaugeIoFinish(false);
      io.startedAt = millis();
      ++io.phase;
      return 0;
    case 6:
      if (millis() - io.startedAt < 20) return 0;
      io.phase = 7;
      return 0;
    default:
      io.phase -= 7;
      const int result = gaugeReadBlockStep(io, io.verified);
      io.phase += 7;
      if (result == 0) return 0;
      return gaugeIoFinish(result > 0 && std::memcmp(io.bytes, io.verified, 32) == 0);
  }
}

int gaugeIoRun(GaugeIoKind kind, uint16_t argument, const uint8_t *bytes) {
  GaugeIo &io = fuelGaugeRuntime.engine.io;
  if (io.kind == GaugeIoKind::None) {
    io = GaugeIo{};
    io.kind = kind;
    io.command = argument;
    io.reg = static_cast<uint8_t>(argument);
    io.subclass = static_cast<uint8_t>(argument);
    io.startedAt = millis();
    if (bytes) std::memcpy(io.bytes, bytes, 32);
  }
  switch (io.kind) {
    case GaugeIoKind::Word: return gaugeWordStep(io);
    case GaugeIoKind::Command:
    case GaugeIoKind::Control: return gaugeCommandStep(io);
    case GaugeIoKind::ReadBlock: return gaugeReadBlockStep(io, io.bytes);
    case GaugeIoKind::WriteBlock: return gaugeWriteBlockStep(io);
    default: return 0;
  }
}

bool gaugeConfigure(uint16_t &capacity, bool adoptCapacity) {
  if (fuelGaugeBusy() || !fuelGaugeRuntime.configurationAllowed) return false;
  if (!b_hasFuelGauge) {
    const bool result = fuelGaugeBegin(capacity, adoptCapacity);
    capacity = fuelGaugeRuntime.desiredCapacity;
    return result;
  }
  fuelGaugeRuntime.desiredCapacity = capacity;
  fuelGaugeRuntime.adoptCapacity = adoptCapacity;
  gaugeStartOperation(false);
  const bool result = fuelGaugeDrain();
  capacity = fuelGaugeRuntime.desiredCapacity;
  return result;
}
