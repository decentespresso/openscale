import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>
#include "calibration_validation.h"
using std::max;
using std::isfinite;
#define ADS1232ADC
#define RTC_DATA_ATTR
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2, BATTERY_CHARGING = 10;
constexpr int SCALE_DOUT = 11, SCALE_SCLK = 12, SCALE_PDWN = 13, SCALE_A0 = -1, PWR_CTRL = 3;
constexpr int ESP_SLEEP_WAKEUP_TIMER = 1, ESP_SLEEP_WAKEUP_EXT1 = 2, ESP_EXT1_WAKEUP_ANY_LOW = 0;
constexpr uint64_t PIN_BITMASK = (1ULL << 1) | (1ULL << 2) | (1ULL << 10);
constexpr float lowBatteryThreshold = 3.2f;
using gpio_num_t = int;
int i_wow_interval = 1, i_lowBatteryCount = 0, GPIO_power_on_with = -1;
float f_calibration_value = 10, f_batteryVoltage = 4;
unsigned long nowMs = 0, cpuMhz = 240, readyAt = 500, pressAt = ULONG_MAX;
unsigned long pressDuration = 500;
int pressPin = BUTTON_CIRCLE, wakeCause = ESP_SLEEP_WAKEUP_TIMER;
int liveAdcs = 0, endedAdcs = 0, begunAdcs = 0;
bool badSample = false, sampleAvailable = true, ext1 = false, pressOnRearm = false;
bool rtcInputsReady = false;
uint64_t timerUs = 0;
bool holds[64] = {};
int levels[64] = {};
int32_t sampleRaw = 1000;
std::vector<int32_t> sampleSequence;
unsigned int samplesRead = 0;
unsigned int invalidSampleAt = 0;
int32_t lastSampleRaw = 1000;
struct DebugInfo {
  int validSamples = 1;
  int32_t smoothedValue = 1000;
  int32_t rawValue = 1000;
  bool dataOutOfRange = false;
  bool signalTimeout = false;
};
struct Scale { DebugInfo info; DebugInfo getDebugInfo() { return info; } } scale;
unsigned long millis() { return nowMs; }
void delay(unsigned long ms) { nowMs += ms; }
unsigned long getCpuFrequencyMhz() { return cpuMhz; }
bool setCpuFrequencyMhz(unsigned long mhz) {
#ifdef CONFIG_PM_ENABLE
  assert(false && "WoW must not change the clock outside ESP-IDF PM");
#endif
  cpuMhz = mhz;
  return true;
}
int esp_sleep_get_wakeup_cause() { return wakeCause; }
int rtc_gpio_get_level(int pin) {
  if (!rtcInputsReady) return LOW;
  return pin == pressPin && nowMs >= pressAt && nowMs - pressAt < pressDuration ? LOW : HIGH;
}
int digitalRead(int pin) { assert(pin == SCALE_DOUT); return sampleAvailable && nowMs >= readyAt ? LOW : HIGH; }
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { levels[pin] = value; }
void gpio_hold_en(int pin) { holds[pin] = true; }
void gpio_hold_dis(int pin) { holds[pin] = false; }
void gpio_deep_sleep_hold_en() {}
void gpio_deep_sleep_hold_dis() {}
void configureWakePinsForDeepSleep() { rtcInputsReady = true; }
void esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int) {
  assert(mask == PIN_BITMASK);
  ext1 = true;
  if (pressOnRearm) pressAt = nowMs;
}
void esp_sleep_enable_timer_wakeup(uint64_t us) { timerUs = us; }
void esp_sleep_disable_wakeup_source(int cause) { assert(cause == ESP_SLEEP_WAKEUP_TIMER); timerUs = 0; }
struct Sleep {};
void esp_deep_sleep_start() { throw Sleep{}; }
struct ADS1232_ADC {
  bool poweredDown = false;
  ADS1232_ADC(int, int, int, int) { liveAdcs++; }
  void begin() { begunAdcs++; }
  bool update() {
    lastSampleRaw = samplesRead < sampleSequence.size() ? sampleSequence[samplesRead] : sampleRaw;
    samplesRead++;
    readyAt = nowMs + 100;
    return true;
  }
  DebugInfo getDebugInfo() { return {1, 1000, lastSampleRaw, badSample || samplesRead == invalidSampleAt}; }
  void powerDown() { poweredDown = true; }
  void end() { assert(poweredDown); liveAdcs--; endedAdcs++; }
};
"""

CHECKS = r"""
char menuWakeOnWeightLabel[24];
std::string actionMessage, actionMessage2;
int t_actionMessageDelay = 0, menuNotifications = 0, settingsWrites = 0;
int storedInterval = -1;
const int KEY_WOW_INTERVAL = 1;
bool storageWorks = true;
bool storagePutInt(int, int value) {
  settingsWrites++;
  if (storageWorks) storedInterval = value;
  return storageWorks;
}
void menuActionMessageChanged() { menuNotifications++; }
MENU_FUNCTIONS

void resetBoot() {
  nowMs = 0; cpuMhz = 240; timerUs = 0; ext1 = false;
  rtcInputsReady = false;
  GPIO_power_on_with = -1; wowButtonWake = false; wowTimerArmedThisBoot = false;
  liveAdcs = 0; endedAdcs = 0; begunAdcs = 0;
  pressAt = ULONG_MAX; pressOnRearm = false; pressPin = BUTTON_CIRCLE;
  readyAt = 500; sampleAvailable = true; badSample = false; sampleRaw = 1000;
  samplesRead = 0; invalidSampleAt = 0; sampleSequence.clear();
  wowWakeDiagnostics = {};
  wakeCause = ESP_SLEEP_WAKEUP_TIMER;
  for (int pin = 0; pin < 64; pin++) { holds[pin] = true; levels[pin] = LOW; }
}
bool tick() {
  try { wowMicroWakeOrContinue(); }
  catch (const Sleep &) {
    assert(liveAdcs == 0 && begunAdcs == endedAdcs && ext1);
    for (int pin : {SCALE_SCLK, SCALE_PDWN, SCALE_DOUT, PWR_CTRL}) assert(holds[pin]);
    assert(levels[PWR_CTRL] == LOW);
    return false;
  }
  assert(liveAdcs == 0 && begunAdcs == endedAdcs && cpuMhz == 240);
  return true;
}
void arm() { wowCaptureBaselineForSleep(); assert(wowRtc.armed); }
int main() {
  static_assert(sizeof(WowRtcState) == 20);
  for (bool outOfRange : {false, true}) {
    for (bool timeout : {false, true}) {
      resetBoot(); arm();
      scale.info.dataOutOfRange = outOfRange;
      scale.info.signalTimeout = timeout;
      wowCaptureBaselineForSleep();
      assert(bool(wowRtc.armed) == (!outOfRange && !timeout));
      scale.info = {};
      arm();
    }
  }
  for (bool missing : {false, true}) {
    for (int pin : {BUTTON_CIRCLE, BUTTON_SQUARE, BATTERY_CHARGING}) {
      for (unsigned long start = 0; start <= (missing ? 998UL : 700UL); start += 2) {
        resetBoot(); arm(); pressAt = start; pressPin = pin; sampleAvailable = !missing;
        assert(tick());
        assert(GPIO_power_on_with == pin && !wowRtc.armed);
        assert(wowButtonWake == (pin != BATTERY_CHARGING));
        assert(nowMs <= start + 2);
      }
    }
  }
  resetBoot(); arm(); pressOnRearm = true;
  assert(tick() && wowButtonWake && !wowRtc.armed);
  resetBoot(); arm(); pressOnRearm = true; badSample = true;
  assert(tick() && wowButtonWake && timerUs == 0);
  for (int32_t raw : {499, 1501, INT32_MIN, INT32_MAX}) {
    resetBoot(); arm(); sampleRaw = raw;
    assert(tick() && !wowRtc.armed && GPIO_power_on_with == -1);
  }
  for (int32_t raw : {500, 1000, 1500}) {
    resetBoot(); arm(); sampleRaw = raw;
    assert(!tick() && wowRtc.armed && timerUs == 2000000);
  }
  for (const auto &sequence : std::vector<std::vector<int32_t>>{
      {100000, 1000, 1000}, {1000, 100000, 1000}, {1000, 1000, 100000},
      {1000, 2000, 0}, {1000, 0, 2000}}) {
    resetBoot(); arm(); sampleSequence = sequence;
    assert(!tick() && wowRtc.armed && samplesRead == 3);
    assert(wowWakeDiagnostics.samplesRead == 3);
    for (unsigned int index = 0; index < 3; index++) {
      assert(wowWakeDiagnostics.raw[index] == sequence[index]);
    }
    assert(wowRtc.baselineRaw == 1000);
  }
  for (const auto &sequence : std::vector<std::vector<int32_t>>{
      {100000, 1600, 1700}, {-100000, 400, 300}}) {
    resetBoot(); arm(); sampleSequence = sequence;
    assert(tick() && !wowRtc.armed && samplesRead == 3);
  }
  resetBoot(); arm(); readyAt = 950;
  assert(!tick() && wowRtc.armed && samplesRead == 1 && nowMs == 1000);
  for (unsigned int invalidAt : {2U, 3U}) {
    resetBoot(); arm(); sampleRaw = 2000; invalidSampleAt = invalidAt;
    assert(!tick() && wowRtc.armed && timerUs == 2000000);
  }
  resetBoot(); arm(); invalidSampleAt = 1;
  assert(!tick() && wowRtc.armed && timerUs == 2000000);
  for (float factor : {100.0f, -100.0f}) {
    f_calibration_value = factor;
    for (int32_t change : {-5000, -4999, -1000, 0, 1000, 4999, 5000}) {
      resetBoot(); arm(); sampleRaw = 1000 + change;
      assert(wowRtc.thresholdRaw == 5000);
      assert(!tick() && wowRtc.armed);
    }
    for (int32_t change : {-5001, 5001}) {
      resetBoot(); arm(); sampleRaw = 1000 + change;
      assert(tick() && !wowRtc.armed);
    }
  }
  f_calibration_value = 10;
  for (bool missing : {false, true}) {
    resetBoot(); arm();
    for (int failure = 1; failure <= 1000; failure++) {
      resetBoot(); sampleAvailable = !missing; badSample = !missing;
      assert(!tick());
      assert(wowRtc.armed && timerUs == 2000000);
      assert(wowRtc.baselineRaw == 1000);
    }
    resetBoot(); sampleRaw = 1501;
    assert(tick() && !wowRtc.armed);
  }
  for (int interval = 1; interval <= 3; interval++) {
    resetBoot(); i_wow_interval = interval; arm();
    for (int count = 1; count <= 70000; count++) {
      resetBoot(); assert(!tick());
      assert(wowRtc.armed);
      assert(timerUs == wowIntervalUs[interval]);
    }
    resetBoot(); sampleRaw = 1501;
    assert(tick() && !wowRtc.armed);
  }
  for (int interval : {-1, 0, 4, 100}) {
    resetBoot(); i_wow_interval = interval; wowCaptureBaselineForSleep();
    wowArmSleepTimer(); assert(!wowRtc.armed && timerUs == 0 && tick());
  }
  i_wow_interval = 1;
  resetBoot(); arm(); wowArmSleepTimer(); assert(timerUs == 2000000);
  i_wow_interval = 0; wowCaptureBaselineForSleep(); wowArmSleepTimer();
  assert(!wowRtc.armed && timerUs == 0);
  i_wow_interval = 1;
  for (float calibration : {CALIBRATION_VALUE_DEFAULT, 0.0f, 0.1f, NAN, INFINITY, 1e30f}) {
    f_calibration_value = calibration; wowCaptureBaselineForSleep(); assert(!wowRtc.armed);
  }
  f_calibration_value = -10;
  resetBoot(); arm(); assert(wowRtc.thresholdRaw == 500);
  f_batteryVoltage = 3.1f; wowCaptureBaselineForSleep(); assert(!wowRtc.armed);
  f_batteryVoltage = 4; i_lowBatteryCount = 1;
  wowCaptureBaselineForSleep(); assert(!wowRtc.armed);
  i_lowBatteryCount = 0;
  resetBoot(); arm(); wakeCause = ESP_SLEEP_WAKEUP_EXT1;
  assert(tick() && begunAdcs == 0 && !wowButtonWake);
  resetBoot(); arm(); wowRtc.magic = 0;
  assert(tick() && begunAdcs == 0);
  for (float calibration : {CALIBRATION_VALUE_DEFAULT, 0.0f, NAN, INFINITY, 1e30f}) {
    f_calibration_value = calibration;
    i_wow_interval = 0;
    settingsWrites = 0;
    const int before = menuNotifications;
    cycleWakeOnWeight();
    assert(i_wow_interval == 0 && settingsWrites == 0);
    assert(actionMessage2 == "Please calibrate" && menuNotifications == before + 1);
    assert(t_actionMessageDelay == 2000);
    for (int interval : {1, 2, 3}) {
      i_wow_interval = interval;
      cycleWakeOnWeight();
      assert(i_wow_interval == 0 && storedInterval == 0 && actionMessage2 == "Off");
    }
  }
  f_calibration_value = 10;
  for (int expected : {1, 2, 3, 0}) {
    cycleWakeOnWeight();
    assert(i_wow_interval == expected && storedInterval == expected);
  }
  storageWorks = false;
  cycleWakeOnWeight();
  assert(i_wow_interval == 0 && actionMessage == "Save Failed");
  std::puts("WoW runtime checks passed: button sweep, charging, cleanup, failures, recovery, unlimited standby, gates");
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    parameter = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    state = parameter[parameter.index("struct WowRtcState {"):]
    state = state[:state.index("#endif")]
    wow = (ROOT / "include/wake_on_weight.h").read_text(encoding="utf-8")
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    menuFunctions = menu[menu.index("void updateWakeOnWeightLabel() {"):]
    menuFunctions = menuFunctions[:menuFunctions.index("#endif")]
    source = HARNESS + state + re.sub(r"^#include[^\n]*", "", wow, flags=re.MULTILINE) + CHECKS.replace("MENU_FUNCTIONS", menuFunctions)
    with tempfile.TemporaryDirectory(prefix="wow-test-") as directory:
        cpp = Path(directory) / "wow.cpp"
        binary = Path(directory) / "wow-test.exe"
        cpp.write_text(source, encoding="utf-8")
        for flags in ([], ["-DCONFIG_PM_ENABLE=1"]):
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
