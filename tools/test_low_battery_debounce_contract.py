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


def assert_fresh_low_samples(body):
    sample = block_body(body, "bool processNewBatterySample()")
    sequence = sample.index("batterySamples.shouldEvaluate(powerCadence.batterySampleSequence)")
    increment = sample.index("i_lowBatteryCount++;")
    shutdown = sample.index("shut_down_low_battery(f_batteryVoltage);")
    if not sequence < increment < shutdown:
        raise AssertionError("low-battery debounce does not require a fresh sample before shutdown")
    if "EnergyRuntimePolicy::lowBatteryConfirmed(i_lowBatteryCount)" not in sample:
        raise AssertionError("low-battery debounce does not require confirmation")
    if "t_power_off" in sample:
        raise AssertionError("low-battery sampling changes the inactivity timer")


def main():
    power = POWER_HEADER.read_text(encoding="utf-8")
    assert_fresh_low_samples(power)
    for signature in ("void power_off(int min)", "void power_off(double sec)"):
        body = block_body(power, signature)
        if "if (processNewBatterySample()) return;" not in body:
            raise AssertionError(f"{signature} bypasses fresh low-battery evaluation")

    print("low-battery debounce contract tests passed")


if __name__ == "__main__":
    main()
