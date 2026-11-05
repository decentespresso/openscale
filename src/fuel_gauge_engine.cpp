#include "fuel_gauge.h"
#include "fuel_gauge_internal.h"
#include <cstring>

static bool gaugeCleanupStage(GaugeStage stage) {
  return stage >= GaugeStage::CleanupIdentify && stage <= GaugeStage::Finish;
}

static void gaugeBeginCleanup() {
  FuelGaugeEngine &engine = fuelGaugeRuntime.engine;
  if (!engine.cleanupStarted) {
    engine.cleanupStarted = true;
    engine.cleanupStartedAt = millis();
  }
  engine.stage = GaugeStage::CleanupIdentify;
}

static void gaugeFailedOperation() {
  FuelGaugeEngine &engine = fuelGaugeRuntime.engine;
  engine.successful = false;
  if (!gaugeCleanupStage(engine.stage) ||
      (engine.stage >= GaugeStage::VerifyChemistry && engine.stage <= GaugeStage::VerifyFlags)) gaugeBeginCleanup();
}

static bool gaugeAdvance(int result, GaugeStage next) {
  if (result < 0) gaugeFailedOperation();
  if (result != 1) return false;
  fuelGaugeRuntime.engine.stage = next;
  return true;
}

void gaugeStartOperation(bool cleanupOnly) {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  runtime.engine = FuelGaugeEngine{};
  runtime.engine.requestedCapacity = runtime.desiredCapacity;
  runtime.engine.capacity = runtime.desiredCapacity;
  runtime.engine.adoptCapacity = runtime.adoptCapacity;
  runtime.engine.wasSealed = runtime.resealPending || runtime.sealRequired;
  runtime.engine.cleanupOnly = cleanupOnly;
  runtime.configurationPending = false;
  runtime.recoveryPending = false;
  runtime.ready = false;
  fuelGaugePauseCharging();
  if (cleanupOnly) gaugeBeginCleanup();
  else runtime.engine.stage = GaugeStage::Identify;
}

static void gaugeFinishOperation() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  FuelGaugeEngine &engine = runtime.engine;
  const bool configured = engine.successful && !engine.cleanupOnly &&
                          !runtime.quiesceRequested && !runtime.resealPending;
  if (configured && runtime.desiredCapacity == engine.requestedCapacity) {
    if (runtime.desiredCapacity != engine.capacity) {
      runtime.desiredCapacity = engine.capacity;
      runtime.capacityPersistPending = true;
    }
    runtime.ready = !runtime.configurationPending;
  }
  if (engine.cleanupOnly && !runtime.quiesceRequested) runtime.configurationPending = true;
  if (!engine.successful && !runtime.quiesceRequested) runtime.recoveryPending = true;
  runtime.lastRecovery = millis();
  runtime.lastPoll = millis() - 1000;
  if (runtime.quiesceRequested) runtime.quiesceComplete = true;
  engine.cleanupStarted = false;
  engine.stage = GaugeStage::Idle;
}

static void gaugeFailCleanup() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  runtime.ready = false;
  runtime.quiesceFailed = runtime.quiesceRequested;
  runtime.recoveryPending = true;
  runtime.lastRecovery = millis();
  runtime.engine.io.kind = GaugeIoKind::None;
  runtime.engine.stage = GaugeStage::Idle;
  runtime.engine.cleanupStarted = false;
  fuelGaugePauseCharging();
}

static bool gaugeObserveFlags(uint16_t flags, bool cleanup = false) {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  FuelGaugeEngine &engine = runtime.engine;
  if (!(flags & gaugePorFlag) && !runtime.porSeen) return true;
  runtime.porSeen = true;
  if (runtime.desiredCapacity == 0) {
    if (!fuelGaugeStoreCapacity(700)) {
      engine.successful = false;
      engine.cleanupOnly = true;
      engine.capacityPersistenceFailed = true;
      if (engine.wasSealed && !engine.currentlySealed) {
        if (!engine.cleanupStarted) {
          engine.cleanupStarted = true;
          engine.cleanupStartedAt = millis();
        }
        engine.stage = GaugeStage::Seal;
      } else gaugeFailCleanup();
      return false;
    }
    runtime.desiredCapacity = 700;
    runtime.capacityPersistPending = true;
    if (engine.requestedCapacity == 0 && (engine.capacity == 0 || engine.capacity == 700)) {
      engine.requestedCapacity = 700;
      engine.capacity = 700;
    }
  }
  if (engine.requestedCapacity == 0 && engine.capacity != 0 && engine.capacity != 700) {
    engine.successful = false;
    engine.cleanupOnly = true;
    runtime.configurationPending = true;
    if (!cleanup) {
      gaugeBeginCleanup();
      return false;
    }
  }
  return true;
}

static void gaugePlanConfiguration() {
  FuelGaugeEngine &engine = fuelGaugeRuntime.engine;
  const uint16_t actualCapacity = static_cast<uint16_t>(engine.state[6]) << 8 | engine.state[7];
  if (engine.capacity == 0) {
    engine.capacity = engine.adoptCapacity && !fuelGaugeRuntime.porSeen && !(engine.flags & gaugePorFlag) &&
                      actualCapacity >= 300 && actualCapacity <= 2000 ? actualCapacity : 700;
  }
  engine.changeChemistry = engine.chemistry != 0x1202;
  engine.changeCapacity = actualCapacity != engine.capacity;
  engine.changeGain = (engine.calibration[5] & 0x80) != 0;
  if (engine.changeChemistry || engine.changeCapacity || engine.changeGain ||
      (engine.flags & (gaugePorFlag | gaugeConfigFlag))) {
    engine.stage = GaugeStage::Enter;
  } else {
    gaugeBeginCleanup();
  }
}

static void gaugeServiceSample() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  FuelGaugeEngine &engine = runtime.engine;
  static constexpr uint8_t registers[] = {0x06, 0x04, 0x1c, 0x1e, 0x10, 0x18, 0x0c, 0x0e, 0x20, 0x06};
  const int result = gaugeIoRun(GaugeIoKind::Word, registers[engine.sampleIndex]);
  if (result == 0) return;
  const uint16_t value = engine.io.word;
  if (result > 0 && (engine.sampleIndex == 0 || engine.sampleIndex == 9) && (value & gaugePorFlag)) runtime.porSeen = true;
  if (result < 0 || millis() - engine.sampleStartedAt >= 2500 ||
      ((engine.sampleIndex == 0 || engine.sampleIndex == 9) &&
       (value & (gaugePorFlag | gaugeConfigFlag)))) {
    fuelGaugePauseCharging();
    runtime.ready = false;
    runtime.lastRecovery = millis();
    engine.stage = GaugeStage::Idle;
    return;
  }
  FuelGaugeReading &reading = runtime.pendingReading;
  switch (engine.sampleIndex) {
    case 0: reading.flags = value; break;
    case 1: reading.voltageMv = value; break;
    case 2: reading.soc = value; break;
    case 3: reading.temperature = value; break;
    case 4: reading.currentMa = static_cast<int16_t>(value); break;
    case 5: reading.powerMw = static_cast<int16_t>(value); break;
    case 6: reading.capacityMah = value; break;
    case 7: reading.fullCapacityMah = value; break;
    case 8: reading.soh = value & 0xff; break;
    default: reading.flags = value; break;
  }
  if (++engine.sampleIndex < sizeof(registers)) return;
  engine.stage = GaugeStage::Idle;
  const bool valid = reading.soc <= 100 && reading.voltageMv > 0 &&
                     reading.voltageMv <= 5000 && reading.soh <= 100 &&
                     reading.temperature >= 1731 && reading.temperature <= 4732;
  if (!valid) {
    fuelGaugePauseCharging();
    runtime.ready = false;
    runtime.lastRecovery = millis();
    return;
  }
  reading.valid = true;
  reading.sequence = runtime.reading.sequence + 1;
  reading.sampledAt = millis();
  runtime.reading = reading;
  if (reading.soc < 10 && !runtime.lowSocNotified) {
    Serial.printf("fuelGauge: battery low, SOC %u%%\n", reading.soc);
  }
  runtime.lowSocNotified = reading.soc < 10;
  gaugeApplyProtection();
}

void gaugeServiceOperation() {
  FuelGaugeRuntime &runtime = fuelGaugeRuntime;
  FuelGaugeEngine &engine = runtime.engine;
  if (engine.cleanupStarted && millis() - engine.cleanupStartedAt >= 8000) {
    gaugeFailCleanup();
    return;
  }
  if ((runtime.quiesceRequested || (engine.cleanupOnly && engine.cleanupStarted)) &&
      !engine.cleanupUnsealing && !gaugeCleanupStage(engine.stage) &&
      engine.io.kind == GaugeIoKind::None) {
    engine.cleanupOnly = true;
    gaugeBeginCleanup();
  }
  switch (engine.stage) {
    case GaugeStage::Identify:
    case GaugeStage::CleanupIdentify: {
      const GaugeStage next = engine.stage == GaugeStage::Identify ? GaugeStage::Status : GaugeStage::CleanupStatus;
      const int result = gaugeIoRun(GaugeIoKind::Control, 0x0001);
      if (result < 0) gaugeFailCleanup();
      else if (result == 1) {
        if (engine.io.word != 0x0427) gaugeFailCleanup();
        else engine.stage = next;
      }
      break;
    }
    case GaugeStage::Status:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0), GaugeStage::Flags)) {
        engine.wasSealed = engine.wasSealed || (engine.io.word & 0x2000);
        if (engine.wasSealed) {
          if (!fuelGaugeStoreReseal(true)) {
            engine.successful = false;
            engine.stage = GaugeStage::Finish;
          } else {
            runtime.resealPending = true;
            runtime.sealRequired = true;
            engine.stage = GaugeStage::UnsealFirst;
          }
        }
      }
      break;
    case GaugeStage::UnsealFirst:
      gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x8000), GaugeStage::UnsealSecond);
      break;
    case GaugeStage::UnsealSecond:
      gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x8000), GaugeStage::UnsealStatus);
      break;
    case GaugeStage::UnsealStatus:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0), engine.cleanupUnsealing ? GaugeStage::CleanupFlags : GaugeStage::Flags)) {
        engine.cleanupUnsealing = false;
        engine.currentlySealed = (engine.io.word & 0x2000) != 0;
        if (engine.currentlySealed) gaugeFailedOperation();
      }
      break;
    case GaugeStage::Flags:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Word, 0x06), GaugeStage::Chemistry)) {
        engine.flags = engine.io.word;
        gaugeObserveFlags(engine.flags);
      }
      break;
    case GaugeStage::Chemistry:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0x0008), GaugeStage::Capacity)) engine.chemistry = engine.io.word;
      break;
    case GaugeStage::Capacity:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 82), GaugeStage::Calibration)) std::memcpy(engine.state, engine.io.bytes, 32);
      break;
    case GaugeStage::Calibration:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 105), GaugeStage::Plan)) std::memcpy(engine.calibration, engine.io.bytes, 32);
      break;
    case GaugeStage::Plan:
      gaugePlanConfiguration();
      break;
    case GaugeStage::Enter:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x0013), GaugeStage::EnterFlags)) {
        engine.waitStartedAt = millis();
        engine.lastFlagPoll = millis();
      }
      break;
    case GaugeStage::EnterFlags:
      if (engine.io.kind == GaugeIoKind::None && millis() - engine.waitStartedAt >= 3000) {
        gaugeFailedOperation();
      } else if (engine.io.kind != GaugeIoKind::None || millis() - engine.lastFlagPoll >= 500) {
        const int result = gaugeIoRun(GaugeIoKind::Word, 0x06);
        if (result < 0) gaugeFailedOperation();
        if (result == 1 && !gaugeObserveFlags(engine.io.word)) break;
        if (result == 1 && (engine.io.word & gaugeConfigFlag)) {
          engine.stage = GaugeStage::EnterSettle;
          engine.waitStartedAt = millis();
        } else if (result == 1) {
          engine.lastFlagPoll = millis();
        }
      }
      break;
    case GaugeStage::EnterSettle:
      if (millis() - engine.waitStartedAt >= 1000) engine.stage = GaugeStage::ChangeChemistry;
      break;
    case GaugeStage::ChangeChemistry:
      if (!engine.changeChemistry) engine.stage = GaugeStage::ReadCapacity;
      else gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x0031), GaugeStage::ReadCapacity);
      break;
    case GaugeStage::ReadCapacity:
      if (!engine.changeCapacity) engine.stage = GaugeStage::ReadCalibration;
      else if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 82), GaugeStage::WriteCapacity)) {
        std::memcpy(engine.state, engine.io.bytes, 32);
        engine.state[6] = engine.capacity >> 8;
        engine.state[7] = engine.capacity & 0xff;
      }
      break;
    case GaugeStage::WriteCapacity:
      gaugeAdvance(gaugeIoRun(GaugeIoKind::WriteBlock, 82, engine.state), GaugeStage::ReadCalibration);
      break;
    case GaugeStage::ReadCalibration:
      if (!engine.changeGain) gaugeBeginCleanup();
      else if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 105), GaugeStage::WriteCalibration)) {
        std::memcpy(engine.calibration, engine.io.bytes, 32);
        engine.calibration[5] &= 0x7f;
      }
      break;
    case GaugeStage::WriteCalibration:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::WriteBlock, 105, engine.calibration), GaugeStage::CleanupFlags)) gaugeBeginCleanup();
      break;
    case GaugeStage::CleanupStatus:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0), GaugeStage::CleanupFlags)) {
        engine.currentlySealed = (engine.io.word & 0x2000) != 0;
        engine.wasSealed = engine.wasSealed || engine.currentlySealed;
      }
      break;
    case GaugeStage::CleanupFlags:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Word, 0x06), GaugeStage::VerifyChemistry)) {
        engine.flags = engine.io.word;
        if (!gaugeObserveFlags(engine.flags, true)) break;
        if (engine.flags & (gaugeConfigFlag | gaugePorFlag)) {
          if (!engine.currentlySealed) engine.stage = GaugeStage::Exit;
          else if (runtime.resealPending || fuelGaugeStoreReseal(true)) {
            runtime.resealPending = true;
            runtime.sealRequired = true;
            engine.cleanupUnsealing = true;
            engine.stage = GaugeStage::UnsealFirst;
          } else gaugeFailCleanup();
        }
        else if (!engine.successful || engine.cleanupOnly) engine.stage = engine.wasSealed ? GaugeStage::Seal : GaugeStage::FinalFlags;
      }
      break;
    case GaugeStage::Exit:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x0042), GaugeStage::ExitFlags)) {
        engine.waitStartedAt = millis();
        engine.lastFlagPoll = millis();
      }
      break;
    case GaugeStage::ExitFlags:
      if (engine.io.kind == GaugeIoKind::None && millis() - engine.waitStartedAt >= 3000) {
        engine.stage = GaugeStage::Exit;
        engine.successful = false;
      } else if (engine.io.kind != GaugeIoKind::None || millis() - engine.lastFlagPoll >= 500) {
        const int result = gaugeIoRun(GaugeIoKind::Word, 0x06);
        if (result < 0) gaugeFailedOperation();
        if (result == 1 && !gaugeObserveFlags(engine.io.word, true)) break;
        if (result == 1 && !(engine.io.word & (gaugeConfigFlag | gaugePorFlag))) {
          engine.flags = engine.io.word;
          engine.stage = GaugeStage::ExitSettle;
          engine.waitStartedAt = millis();
        } else if (result == 1) engine.lastFlagPoll = millis();
      }
      break;
    case GaugeStage::ExitSettle:
      if (millis() - engine.waitStartedAt >= 2000) {
        engine.stage = engine.successful && !engine.cleanupOnly ? GaugeStage::VerifyChemistry :
                       engine.wasSealed ? GaugeStage::Seal : GaugeStage::FinalFlags;
      }
      break;
    case GaugeStage::VerifyChemistry:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0x0008), GaugeStage::VerifyCapacity) && engine.io.word != 0x1202) gaugeFailedOperation();
      break;
    case GaugeStage::VerifyCapacity:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 82), GaugeStage::VerifyCalibration) &&
          (static_cast<uint16_t>(engine.io.bytes[6]) << 8 | engine.io.bytes[7]) != engine.capacity) gaugeFailedOperation();
      break;
    case GaugeStage::VerifyCalibration:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::ReadBlock, 105), GaugeStage::VerifyFlags) && (engine.io.bytes[5] & 0x80)) gaugeFailedOperation();
      break;
    case GaugeStage::VerifyFlags:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Word, 0x06), engine.wasSealed ? GaugeStage::Seal : GaugeStage::FinalFlags)) {
        if (!gaugeObserveFlags(engine.io.word, true)) break;
        if (engine.io.word & (gaugeConfigFlag | gaugePorFlag)) gaugeFailedOperation();
      }
      break;
    case GaugeStage::Seal:
      gaugeAdvance(gaugeIoRun(GaugeIoKind::Command, 0x0020), GaugeStage::SealStatus);
      break;
    case GaugeStage::SealStatus:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Control, 0), GaugeStage::FinalFlags)) {
        engine.currentlySealed = (engine.io.word & 0x2000) != 0;
        if (!engine.currentlySealed) engine.stage = GaugeStage::Seal;
        else if (engine.capacityPersistenceFailed) gaugeFailCleanup();
      }
      break;
    case GaugeStage::FinalFlags:
      if (gaugeAdvance(gaugeIoRun(GaugeIoKind::Word, 0x06), GaugeStage::ClearReseal)) {
        if (!gaugeObserveFlags(engine.io.word, true)) break;
        if (engine.io.word & (gaugeConfigFlag | gaugePorFlag)) engine.stage = GaugeStage::CleanupFlags;
      }
      break;
    case GaugeStage::ClearReseal:
      if (!runtime.resealPending || fuelGaugeStoreReseal(false)) {
        runtime.resealPending = false;
        runtime.sealRequired = false;
        engine.stage = GaugeStage::Finish;
      } else gaugeFailCleanup();
      break;
    case GaugeStage::Finish:
      gaugeFinishOperation();
      break;
    case GaugeStage::Sample:
      gaugeServiceSample();
      break;
    default:
      break;
  }
}
