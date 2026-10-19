import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_auto_off_weight_activity import extract_function


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>

bool b_hasFuelGauge = false;
bool b_ads1115InitFail = false;
float f_batteryVoltage = 0;
float f_batteryCalibrationFactor = 1;
constexpr float adcResolution = 4095;
constexpr float referenceVoltage = 3.3f;
constexpr float dividerRatio = 2;
constexpr float showEmptyBatteryBelowVoltage = 3.4f;
constexpr float showFullBatteryAboveVoltage = 4.1f;
unsigned long t_batteryRefresh = 0;
unsigned long currentTime = 123;
float legacyVoltage = 3.75f;
int gaugePercent = 87;
int gaugeReads = 0;
bool gaugeValid = true;
bool legacyHardware = true;
uint32_t gaugeSequence = 1;
int adcReads = 0;
int batteryStateMux = 0;
volatile int batteryPercentCached = 0;
bool locked = false;
constexpr int BATTERY_PIN = 6;
constexpr const char *KEY_BAT_CAPACITY = "bat_capacity", *KEY_BAT_CAPACITY_SET = "bat_cap_set";
bool storageSucceeds = false;
int capacityWrites = 0, storedCapacity = 0;
struct { bool capacityPersistPending = true; uint16_t desiredCapacity = 1100; } fuelGaugeRuntime;
bool storagePutInt(const char *, int value) {
  ++capacityWrites;
  if (storageSucceeds) storedCapacity = value;
  return storageSucceeds;
}
bool storagePutBool(const char *, bool) { return true; }
struct {
  uint32_t batterySampleSequence = 0;
  uint32_t fuelGaugeSampleSequence = 0;
} powerCadence;
struct FuelGaugeReading { bool valid; uint32_t sequence; uint16_t voltageMv; };
struct {
  int16_t readADC_SingleEnded(int) { ++adcReads; return 0; }
  float computeVolts(int16_t) { return legacyVoltage / 2; }
} ads;

void portENTER_CRITICAL(int *) { assert(!locked); locked = true; }
void portEXIT_CRITICAL(int *) { assert(locked); locked = false; }
unsigned long millis() { return currentTime; }
int analogRead(int) { ++adcReads; return 4095; }
void fuelGaugeLoop() { ++gaugeReads; }
bool fuelGaugeLegacyHardware() { return legacyHardware; }
FuelGaugeReading fuelGaugeReading() { return {gaugeValid, gaugeSequence, 3800}; }
int fuelGaugeSocPercent() { return gaugePercent; }
template<typename T> T constrain(T value, T low, T high) {
  return std::clamp(value, low, high);
}
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

FUNCTIONS

int main() {
  updateBattery(6);
  assert(batteryPercent() == 50);
  assert(f_batteryVoltage == legacyVoltage);
  assert(gaugeReads == 0 && adcReads == 1);
  assert(t_batteryRefresh == currentTime);
  assert(powerCadence.batterySampleSequence == 1);

  legacyVoltage = 3;
  updateBattery(6);
  assert(batteryPercent() == 0);
  legacyVoltage = 4.5f;
  updateBattery(6);
  assert(batteryPercent() == 100);

  b_ads1115InitFail = true;
  updateBattery(6);
  assert(batteryPercent() == 100);
  assert(gaugeReads == 0 && adcReads == 4);

  b_hasFuelGauge = true;
  currentTime = 456;
  updateBattery(6);
  assert(batteryPercent() == 87);
  assert(f_batteryVoltage == 3.8f);
  assert(gaugeReads == 1 && adcReads == 4);
  assert(t_batteryRefresh == currentTime);
  assert(powerCadence.batterySampleSequence == 5);
  for (int i = 0; i < 100; ++i) assert(batteryPercent() == 87);
  assert(gaugeReads == 1 && adcReads == 4);
  assert(!locked);

  gaugeValid = false;
  updateBattery(6);
  assert(batteryPercent() == 87);
  assert(powerCadence.batterySampleSequence == 5);
  assert(t_batteryRefresh == 456);
  gaugeValid = true;
  currentTime = 999;
  updateBattery(6);
  assert(powerCadence.batterySampleSequence == 5 && t_batteryRefresh == 456);
  ++gaugeSequence;
  gaugePercent = 0;
  updateBattery(6);
  assert(batteryPercent() == 0);
  assert(t_batteryRefresh == 999 && powerCadence.batterySampleSequence == 6);
  b_hasFuelGauge = false;
  legacyHardware = false;
  updateBattery(6);
  assert(adcReads == 4 && powerCadence.batterySampleSequence == 6);
  b_hasFuelGauge = true;
  ++gaugeSequence;
  serviceFuelGauge();
  assert(capacityWrites == 1 && fuelGaugeRuntime.capacityPersistPending);
  serviceFuelGauge();
  assert(capacityWrites == 1);
  storageSucceeds = true;
  ++gaugeSequence;
  serviceFuelGauge();
  assert(capacityWrites == 2 && storedCapacity == 1100);
  assert(!fuelGaugeRuntime.capacityPersistPending);
  ++gaugeSequence;
  serviceFuelGauge();
  assert(capacityWrites == 2);
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    power = (ROOT / "include/power.h").read_text(encoding="utf-8")
    functions = "\n".join(extract_function(power, signature) for signature in (
        "int batteryPercent()",
        "void updateBattery(int batteryPin)",
        "void serviceFuelGauge()",
    ))
    with tempfile.TemporaryDirectory(prefix="fuel-gauge-battery-") as directory:
        cpp = Path(directory) / "battery.cpp"
        binary = Path(directory) / ("battery-test.exe" if os.name == "nt" else "battery-test")
        cpp.write_text(HARNESS.replace("FUNCTIONS", functions), encoding="utf-8")
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("fuel gauge battery integration tests passed")


if __name__ == "__main__":
    main()
