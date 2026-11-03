#ifndef FUEL_GAUGE_ENGINE_H
#define FUEL_GAUGE_ENGINE_H

#include <Arduino.h>

enum class GaugeIoKind { None, Word, Command, Control, ReadBlock, WriteBlock };
enum class GaugeStage {
  Idle, Identify, Status, UnsealFirst, UnsealSecond, UnsealStatus, Flags,
  Chemistry, Capacity, Calibration, Plan, Enter, EnterFlags, EnterSettle,
  ChangeChemistry, ReadCapacity, WriteCapacity, ReadCalibration,
  WriteCalibration, CleanupIdentify, CleanupStatus, CleanupFlags, Exit, ExitFlags, ExitSettle,
  VerifyChemistry, VerifyCapacity, VerifyCalibration, VerifyFlags,
  Seal, SealStatus, FinalFlags, ClearReseal, Finish, Sample
};

struct GaugeIo {
  GaugeIoKind kind = GaugeIoKind::None;
  uint8_t phase = 0;
  uint8_t index = 0;
  uint8_t reg = 0;
  uint8_t subclass = 0;
  uint16_t command = 0;
  uint16_t word = 0;
  unsigned long startedAt = 0;
  uint8_t bytes[32] = {};
  uint8_t verified[32] = {};
};

struct FuelGaugeEngine {
  GaugeStage stage = GaugeStage::Idle;
  GaugeIo io;
  uint16_t requestedCapacity = 0;
  uint16_t capacity = 0;
  uint16_t flags = 0;
  uint16_t chemistry = 0;
  uint8_t state[32] = {};
  uint8_t calibration[32] = {};
  uint8_t sampleIndex = 0;
  unsigned long waitStartedAt = 0;
  unsigned long lastFlagPoll = 0;
  unsigned long cleanupStartedAt = 0;
  unsigned long sampleStartedAt = 0;
  bool adoptCapacity = false;
  bool wasSealed = false;
  bool currentlySealed = false;
  bool cleanupUnsealing = false;
  bool successful = true;
  bool cleanupOnly = false;
  bool cleanupStarted = false;
  bool capacityPersistenceFailed = false;
  bool changeChemistry = false;
  bool changeCapacity = false;
  bool changeGain = false;
};

#endif
