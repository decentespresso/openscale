import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
THRESHOLDS = ("0", "0.05", "0.075", "0.1", "0.2")
SCENARIOS = ("additions", "impacts", "tails", "removals", "small", "noise", "tare",
             "mode", "adc", "drift-positive", "drift-negative")
FAST_SCENARIOS = ("additions", "impacts", "drift-positive", "drift-negative")


CPP_PREFIX = r"""
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <string>
#define HDS_ENABLE_GRINDER 0
#define HDS_ENABLE_ENERGY_MENU 0
constexpr unsigned long WEIGHT_BASE_INTERVAL_MS = 100;
unsigned long clockValue = 100;
unsigned long sampleInterval = 100;
unsigned long millis() { return clockValue; }
void delay(unsigned long) {}
struct QuietSerial {
    template<typename T> void print(T) {}
    template<typename T> void print(T, int) {}
    template<typename T> void println(T) {}
    template<typename T> void println(T, int) {}
    void println() {}
    void printf(const char*, ...) {}
} Serial;
struct FakeScale {
    float raw = 0;
    bool tareDone = false;
    bool update() { return true; }
    bool getSignalTimeoutFlag() { return false; }
    float getData() { return raw; }
    bool getTareStatus() {
        const bool completed = tareDone;
        tareDone = false;
        return completed;
    }
    void powerDown() {}
    void powerUp() {}
} scale;
float f_displayedValue = 0;
bool b_bootTare = false;
bool b_tapTraceEnabled = false;
bool b_bootFreshTarePending = false;
bool b_weight_quick_zero = false;
unsigned long t_tareStatus = 0;
unsigned long t_quickZeroStart = 0;
char c_weight[10];
int i_decimal_precision = 1;
void resetAdcRecoveryState() {}
void updatePressSampling() {}
bool refreshScaleDatasetAfterDiscontinuity(const char*) { return true; }
bool tareScaleWhenAdcReady(const char*) { return true; }
void resetScaleOutputAfterAdcDiscontinuity();
void displayEnhancedStatus(float, float, float) {}
void formatFloatSafe(char*, size_t, float, int) {}
void resetTracking();
void resetStableOutput();
bool verifyWeightStability(float);
void performTrackingAdjustment(float);
"""


CPP_TESTS = r"""
void sample(float value, int count) {
    for (int index = 0; index < count; ++index) {
        scale.raw = value;
        pureScale();
        clockValue += sampleInterval;
    }
}

bool settled(float expected) {
    return std::isfinite(f_displayedValue) &&
           std::fabs(f_displayedValue - expected) < .05f &&
           std::isfinite(f_driftCompensation) &&
           std::fabs(f_driftCompensation) <= .05f;
}

bool additions(int impact) {
    sample(0, 10);
    for (int index = 1; index <= 13; ++index) {
        const float weight = index * 2.1f / 13;
        if (impact) sample(weight + .085f, 1);
        if (impact == 2) {
            for (int recovery = 1; recovery <= 10; ++recovery) {
                sample(weight + .085f - recovery * .0085f, 1);
            }
        }
        sample(weight, 20);
    }
    sample(2.1f, 100);
    return settled(2.1f);
}

bool removals() {
    sample(0, 10);
    sample(2.1f, 30);
    for (int index = 1; index <= 13; ++index) {
        const float weight = 2.1f - index * 2.1f / 13;
        sample(weight - .085f, 1);
        sample(weight, 20);
    }
    sample(0, 100);
    return settled(0);
}

bool stationaryNoise() {
    sample(0, 10);
    sample(1, 30);
    for (int index = 0; index < 600; ++index) {
        sample(1.0f + (index % 2 == 0 ? .02f : -.02f), 1);
    }
    sample(1, 100);
    return settled(1);
}

void warmWithDrift() {
    sample(0, 10);
    sample(1, 30);
    const int count = 30000 / sampleInterval;
    for (int index = 1; index <= count; ++index) {
        sample(1.0f + index * .001f * sampleInterval / 100, 1);
    }
    sample(1.3f, 30);
}

bool resetState() {
    return f_driftCompensation == 0 && !scaleDriftState.ready &&
           f_tracking_offset == 0 && !b_tracking_active &&
           f_previous_stable_value == 0 && stableOutputCandidate == 0;
}

bool smallIncrements() {
    sample(0, 10);
    sample(1, 30);
    for (int index = 1; index <= 10; ++index) {
        sample(1.0f + index * .05f, 20);
        if (!settled(1.0f + index * .05f)) return false;
    }
    return settled(1.5f);
}

bool stationaryDrift(float direction) {
    sample(0, 10);
    sample(1, 30);
    const int count = 50000 / sampleInterval;
    for (int index = 1; index <= count; ++index) {
        sample(1.0f + direction * index * .001f * sampleInterval / 100, 1);
    }
    const float finalInput = 1.0f + direction * .5f;
    sample(finalInput, 100);
    const bool enabled = f_maxDriftCompensation > 0;
    const float expected = enabled ? 1.0f : finalInput;
    const float expectedCompensation = enabled ? direction * .5f : 0.0f;
    return std::isfinite(f_displayedValue) &&
           std::fabs(f_displayedValue - expected) < .05f &&
           std::fabs(f_driftCompensation - expectedCompensation) < .05f;
}

bool completedTare() {
    warmWithDrift();
    scale.tareDone = true;
    sample(0, 1);
    if (!resetState() || f_displayedValue != 0) return false;
    sample(0, 30);
    if (!settled(0)) return false;
    sample(2.1f, 30);
    return settled(2.1f);
}

bool switchedMode() {
    const float selected = f_maxDriftCompensation;
    setMaxDriftCompensation(.075f);
    warmWithDrift();
    if (f_driftCompensation < .2f) return false;
    setMaxDriftCompensation(0);
    if (!resetState() || scale.raw != 1.3f) return false;
    sample(1.3f, 30);
    if (!settled(1.3f)) return false;
    setMaxDriftCompensation(selected);
    if (!resetState() || scale.raw != 1.3f) return false;
    sample(1.3f, 30);
    return settled(1.3f);
}

bool adcDiscontinuity() {
    warmWithDrift();
    resetScaleOutputAfterAdcDiscontinuity();
    if (!resetState() || f_displayedValue != 0) return false;
    sample(1.3f, 30);
    return settled(1.3f);
}

int main(int, char** argv) {
    setMaxDriftCompensation(std::strtof(argv[1], nullptr));
    const std::string scenario = argv[2];
    sampleInterval = std::strtoul(argv[3], nullptr, 10);
    const bool passed = scenario == "additions" ? additions(0) :
                        scenario == "impacts" ? additions(1) :
                        scenario == "tails" ? additions(2) :
                        scenario == "removals" ? removals() :
                        scenario == "small" ? smallIncrements() :
                        scenario == "noise" ? stationaryNoise() :
                        scenario == "tare" ? completedTare() :
                        scenario == "mode" ? switchedMode() :
                        scenario == "adc" ? adcDiscontinuity() :
                        scenario == "drift-positive" ? stationaryDrift(1) :
                        scenario == "drift-negative" ? stationaryDrift(-1) : false;
    std::cout << (passed ? "PASS" : "FAIL") << std::fixed << std::setprecision(6)
              << " threshold=" << f_maxDriftCompensation << " scenario=" << scenario
              << " interval=" << sampleInterval
              << " input=" << scale.raw << " displayed=" << f_displayedValue
              << " compensation=" << f_driftCompensation
              << " tracking=" << f_tracking_offset << std::endl;
    return passed ? 0 : 1;
}
"""


def sourceSection(text, start, end):
    first = text.index(start)
    last = text.index(end, first)
    return text[first:last]


def replaySource():
    sketch = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")
    parameter = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
    state = sourceSection(parameter, "static float f_tracking_offset",
                          "static uint8_t g_resetReasonCode")
    pipeline = sourceSection(sketch, "void updateAdaptiveTracking(float current_weight)",
                             "float getTemperatureDriftCompensation()")
    resets = sourceSection(sketch, "void resetTracking() {",
                           "bool wakeScaleFromSoftSleep(")
    adcReset = sourceSection(sketch, "void resetScaleOutputAfterAdcDiscontinuity() {",
                             "void holdScaleOutputAtZero() {")
    return "\n".join((CPP_PREFIX, state, '#include "scale_drift.h"',
                      pipeline, resets, adcReset, CPP_TESTS))


def main():
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        raise RuntimeError("g++ or clang++ is required for the scale drift check")
    with tempfile.TemporaryDirectory(prefix="hds-scale-drift-") as temporary:
        executable = Path(temporary) / ("scale-drift.exe" if os.name == "nt" else "scale-drift")
        subprocess.run(
            [compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-x", "c++", "-",
             "-I", str(ROOT / "include"), "-o", str(executable)],
            input=replaySource(), text=True, check=True,
        )
        failures = []
        for interval, scenarios in ((100, SCENARIOS), (20, FAST_SCENARIOS)):
            for scenario in scenarios:
                for threshold in THRESHOLDS:
                    result = subprocess.run(
                        [str(executable), threshold, scenario, str(interval)],
                        capture_output=True, text=True,
                    )
                    print(result.stdout, end="", flush=True)
                    if result.stderr:
                        print(result.stderr, end="", flush=True)
                    if result.returncode:
                        failures.append(f"{scenario}@{threshold}/{interval}ms")
        if failures:
            raise AssertionError("scale drift checks failed: " + ", ".join(failures))
    print("scale drift compensation checks passed")


if __name__ == "__main__":
    main()
