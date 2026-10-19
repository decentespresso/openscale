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
int adcReads = 0;
int batteryStateMux = 0;
volatile int batteryPercentCached = 0;
bool locked = false;
struct { uint32_t batterySampleSequence = 0; } powerCadence;
struct {
  int16_t readADC_SingleEnded(int) { ++adcReads; return 0; }
  float computeVolts(int16_t) { return legacyVoltage / 2; }
} ads;

void portENTER_CRITICAL(int *) { assert(!locked); locked = true; }
void portEXIT_CRITICAL(int *) { assert(locked); locked = false; }
unsigned long millis() { return currentTime; }
int analogRead(int) { ++adcReads; return 4095; }
float fuelGaugeVoltageV() { ++gaugeReads; return 3.8f; }
int fuelGaugeSocPercent() { ++gaugeReads; return gaugePercent; }
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
  assert(gaugeReads == 2 && adcReads == 4);
  assert(t_batteryRefresh == currentTime);
  assert(powerCadence.batterySampleSequence == 5);
  for (int i = 0; i < 100; ++i) assert(batteryPercent() == 87);
  assert(gaugeReads == 2 && adcReads == 4);
  assert(!locked);

  gaugePercent = 255;
  updateBattery(6);
  assert(batteryPercent() == 100);
  gaugePercent = 0;
  updateBattery(6);
  assert(batteryPercent() == 0);
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
