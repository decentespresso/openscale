from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POWER_HEADER = ROOT / "include" / "power.h"


def function_body(text, name):
    start = text.index(f"void {name}(")
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def main():
    power = POWER_HEADER.read_text(encoding="utf-8")
    shutdown = function_body(power, "shut_down_low_battery")
    espnow_start = shutdown.index("#ifdef ESPNOW")
    espnow_end = shutdown.index("#endif", espnow_start)
    ble_notification = shutdown.index("sendBlePowerOff(3);")

    if ble_notification < espnow_end:
        raise AssertionError("BLE low-battery notification depends on ESPNOW")

    print("low-battery notification contract tests passed")


if __name__ == "__main__":
    main()
