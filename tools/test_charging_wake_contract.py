#!/usr/bin/env python3
import re
from pathlib import Path


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


if __name__ == "__main__":
    test_charging_button_wake_enables_runtime_radios()
    print("charging wake contract tests passed")
