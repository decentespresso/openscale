from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POWER_HEADER = ROOT / "include" / "power.h"


def block_body(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated block: {marker}")


def assert_fresh_low_samples(body, signature):
    awake = block_body(body, "if (!b_softSleep)")
    refresh = awake.index("updateBattery(BATTERY_PIN);")
    increment = awake.index("i_lowBatteryCount++;")
    if refresh > increment:
        raise AssertionError(f"{signature} counts a cached low-battery sample")
    refresh_guard = awake.rfind("if (f_batteryVoltage < lowBatteryThreshold)", 0, refresh)
    if refresh_guard < 0:
        raise AssertionError(f"{signature} refreshes without a low-battery guard")
    if "shut_down_low_battery(f_batteryVoltage);" not in awake:
        raise AssertionError(f"{signature} can shut down from a sleeping cached sample")
    if "t_power_off" in awake:
        raise AssertionError(f"{signature} pauses the inactivity timer during soft sleep")


def main():
    power = POWER_HEADER.read_text(encoding="utf-8")
    assert_fresh_low_samples(
        block_body(power, "bool processLegacyLowBattery()"),
        "processLegacyLowBattery",
    )
    for signature in ("void power_off(int min)", "void power_off(double sec)"):
        if "processLegacyLowBattery()" not in block_body(power, signature):
            raise AssertionError(f"{signature} bypasses the shared low-battery debounce")

    print("low-battery debounce contract tests passed")


if __name__ == "__main__":
    main()
