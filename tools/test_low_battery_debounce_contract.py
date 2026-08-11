from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POWER_HEADER = ROOT / "include" / "power.h"


def function_body(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def assert_fresh_low_samples(body, signature):
    refresh = body.index("updateBattery(BATTERY_PIN);")
    increment = body.index("i_lowBatteryCount++;")
    if refresh > increment:
        raise AssertionError(f"{signature} counts a cached low-battery sample")
    refresh_guard = body.rfind("if (f_batteryVoltage < lowBatteryThreshold)", 0, refresh)
    if refresh_guard < 0:
        raise AssertionError(f"{signature} refreshes without a low-battery guard")


def main():
    power = POWER_HEADER.read_text(encoding="utf-8")
    for signature in ("void power_off(int min)", "void power_off(double sec)"):
        assert_fresh_low_samples(function_body(power, signature), signature)

    print("low-battery debounce contract tests passed")


if __name__ == "__main__":
    main()
