#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"


def read_source():
    return HDS_SOURCE.read_text(encoding="utf-8")


def function_body(text, name):
    match = re.search(rf"\b\w+\s+{re.escape(name)}\(", text)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    opening = text.index("{", match.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def assert_contains(body, snippet):
    if snippet not in body:
        raise AssertionError(f"missing snippet: {snippet}")


def test_charging_button_wake_enables_runtime_radios():
    source = read_source()
    wake = function_body(source, "wakeFromChargingUi")
    assert_contains(wake, "GPIO_power_on_with = buttonPin;")
    assert_contains(wake, "b_showChargingUI = false;")
    assert_contains(wake, "b_is_charging = false;")
    assert_contains(wake, "b_ble_enabled = true;")
    assert_contains(wake, "ble_init();")
    assert_contains(wake, "wifi_init();")

    assert_contains(function_body(source, "buttonCircle_Pressed"), "wakeFromChargingUi(BUTTON_CIRCLE);")
    assert_contains(function_body(source, "buttonSquare_Pressed"), "wakeFromChargingUi(BUTTON_SQUARE);")
    assert_contains(function_body(source, "buttonCircle_LongPressed"), "wakeFromChargingUi(BUTTON_CIRCLE);")
    assert_contains(function_body(source, "buttonSquare_LongPressed"), "wakeFromChargingUi(BUTTON_SQUARE);")


def test_charge_polling_survives_soft_sleep():
    source = read_source()
    loop = function_body(source, "loop")
    charge_gate = "if (powerCadence.chargeCheck.shouldRun(millis(), 200))"
    charge_index = loop.index(charge_gate)
    assert loop.count("checkBattery();") == 1
    assert loop.index("return;", loop.index("if (b_ota)")) < charge_index
    assert charge_index < loop.index("serviceEnergyHousekeeping(millis())")
    assert charge_index < loop.index("power_off(-1)")
    assert charge_index < loop.index("if (!b_softSleep)")
    power = (ROOT / "include/power.h").read_text(encoding="utf-8")
    harness = r'''
#include <cassert>
#include <initializer_list>
#include "energy_runtime_policy.h"
#define V8_1
constexpr int LOW = 0, HIGH = 1, USB_DET = 8;
bool b_is_charging = false, b_softSleep = false;
float f_batteryVoltage = 4.0f;
constexpr float showEmptyBatteryBelowVoltage = 3.4f;
constexpr float showFullBatteryAboveVoltage = 4.1f;
char *c_battery = nullptr;
int usbLevel = LOW;
unsigned long now = 0;
struct { CadenceGate chargeCheck; } powerCadence;
unsigned long millis() { return now; }
int digitalRead(int pin) { assert(pin == USB_DET); return usbLevel; }
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}
@CHECK_BATTERY@
void pollCharging() { @CHARGE_GATE@ { @CHARGE_BODY@ } }
int main() {
  for (bool sleeping : {false, true}) {
    b_softSleep = sleeping;
    powerCadence.chargeCheck = CadenceGate{};
    now = 0;
    usbLevel = LOW;
    pollCharging();
    assert(b_is_charging && c_battery[0] == '6');
    usbLevel = HIGH;
    now = 199;
    pollCharging();
    assert(b_is_charging);
    now = 200;
    pollCharging();
    assert(!b_is_charging && c_battery[0] == '5');
    usbLevel = LOW;
    now = 400;
    pollCharging();
    assert(b_is_charging && c_battery[0] == '6');
    assert(b_softSleep == sleeping);
  }
}
'''
    check_battery = power[power.index("void checkBattery() {"):power.rindex("\n#endif")]
    harness = harness.replace("@CHECK_BATTERY@", check_battery)
    harness = harness.replace("@CHARGE_GATE@", charge_gate)
    harness = harness.replace("@CHARGE_BODY@", block_after(loop, charge_gate))
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    with tempfile.TemporaryDirectory(prefix="charging-wake-") as directory:
        cpp = Path(directory) / "charging.cpp"
        binary = Path(directory) / "charging.exe"
        cpp.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    test_charging_button_wake_enables_runtime_radios()
    test_charge_polling_survives_soft_sleep()
    print("charging wake contract tests passed")
