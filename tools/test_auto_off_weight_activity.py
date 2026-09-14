import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r"""
#include <cassert>
#include <climits>
#include <limits>
#include "auto_off_activity.h"
#include "energy_runtime_policy.h"

unsigned long currentTime = 0;
unsigned long t_power_off = 0;
float f_displayedValue = 0;
bool b_autoSleep = true;
bool b_is_charging = false;
bool lowBattery = false;
int shutdowns = 0;
AutoOffWeightActivityTracker autoOffWeightActivity;
struct { CadenceGate autoOff; } powerCadence;
struct {
  void print(double) {}
  void println(const char *) {}
} Serial;
unsigned long millis() { return currentTime; }
bool processNewBatterySample() { return lowBattery; }
void shut_down_now() { ++shutdowns; }

FUNCTIONS

template <typename Timeout>
void checkAutoOff(Timeout timeout, unsigned long duration) {
  currentTime = 0;
  f_displayedValue = 0;
  shutdowns = 0;
  powerCadence.autoOff = CadenceGate{};
  power_off(static_cast<Timeout>(-1));
  power_off(timeout);
  currentTime = 500;
  f_displayedValue = 2;
  power_off(timeout);
  assert(t_power_off == 500);
  currentTime = duration;
  power_off(timeout);
  assert(shutdowns == 0);
  currentTime = duration + 1000;
  power_off(timeout);
  assert(shutdowns == 1);

  b_autoSleep = false;
  currentTime += 1000;
  power_off(timeout);
  assert(shutdowns == 1);
  b_autoSleep = true;
  b_is_charging = true;
  currentTime += 1000;
  power_off(timeout);
  assert(shutdowns == 1);
  power_off(static_cast<Timeout>(-1));
  assert(t_power_off == currentTime);
  assert(autoOffWeightActivity.windowStartWeight == f_displayedValue);
  b_is_charging = false;

  lowBattery = true;
  currentTime += 500;
  f_displayedValue += 2;
  const unsigned long previousReset = t_power_off;
  power_off(timeout);
  assert(t_power_off == previousReset);
  lowBattery = false;
}

int main() {
  AutoOffWeightActivityTracker tracker;
  assert(!tracker.update(0, 0));
  for (unsigned long now = 100; now <= 10000; now += 100) {
    assert(!tracker.update(now, now % 200 == 0 ? 0.4f : -0.4f));
  }
  tracker.reset(0, 0);
  for (unsigned long now = 100; now <= 10000; now += 100) {
    assert(!tracker.update(now, static_cast<float>(now) * 0.0005f));
  }
  tracker.reset(0, 0);
  assert(!tracker.update(500, 0.5f));
  assert(!tracker.update(900, 0.9f));
  assert(tracker.update(1000, 1));
  assert(tracker.update(2000, 2));
  assert(!tracker.update(2100, 2));
  tracker.reset(0, 0);
  assert(tracker.update(1100, 2));
  tracker.reset(0, 0);
  assert(!tracker.update(1100, 1));
  tracker.reset(0, 0);
  assert(!tracker.update(900, 0));
  assert(tracker.update(1100, 1));
  assert(!tracker.update(1200, 1));
  tracker.reset(0, 0);
  assert(!tracker.update(900, 0));
  assert(tracker.update(1100, -1));
  tracker.reset(0, 0);
  assert(!tracker.update(900, 0));
  assert(!tracker.update(1100, 0.6f));
  assert(tracker.update(1500, 1));
  tracker.reset(ULONG_MAX - 999, 0);
  assert(!tracker.update(ULONG_MAX - 99, 0));
  assert(tracker.update(100, 1));
  tracker.reset(0, 0);
  assert(tracker.update(500, -1));
  assert(!tracker.update(500, -1));
  tracker.reset(ULONG_MAX - 499, 0);
  assert(tracker.update(500, 1));
  tracker.reset(0, 0);
  assert(!tracker.update(100, std::numeric_limits<float>::quiet_NaN()));
  assert(!tracker.update(200, 10));
  assert(tracker.update(700, 11));
  assert(!tracker.update(800, std::numeric_limits<float>::infinity()));
  assert(!tracker.update(900, 12));
  tracker.reset(1000, std::numeric_limits<float>::quiet_NaN());
  assert(!tracker.update(1100, 20));
  checkAutoOff(1, 60000);
  checkAutoOff(2.0, 2000);
}
"""


def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(signature)


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    power = (ROOT / "include/power.h").read_text(encoding="utf-8")
    parameters = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    assert '#include "auto_off_activity.h"' in parameters
    assert "AutoOffWeightActivityTracker autoOffWeightActivity;" in parameters
    functions = "\n".join(extract_function(power, signature) for signature in (
        "void resetAutoOffTimer(unsigned long now)",
        "void evaluateAutoOff(double seconds, bool showCountdown)",
        "void power_off(int min)",
        "void power_off(double sec)",
    ))
    with tempfile.TemporaryDirectory(prefix="auto-off-weight-") as directory:
        cpp = Path(directory) / "auto_off_weight.cpp"
        binary = Path(directory) / ("auto-off-test.exe" if os.name == "nt" else "auto-off-test")
        cpp.write_text(HARNESS.replace("FUNCTIONS", functions), encoding="utf-8")
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("auto-off weight activity tests passed")


if __name__ == "__main__":
    main()
