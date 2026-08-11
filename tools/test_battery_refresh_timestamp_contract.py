from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
POWER_HEADER = ROOT / "include" / "power.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"


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


def main():
    power = POWER_HEADER.read_text(encoding="utf-8")
    hds = HDS_SOURCE.read_text(encoding="utf-8")
    update = function_body(power, "void updateBattery(int batteryPin)")

    if "t_batteryRefresh = millis();" not in update:
        raise AssertionError("battery acquisition does not mark its sample fresh")
    if "t_batteryRefresh = millis();" in function_body(hds, "void loop()"):
        raise AssertionError("battery freshness is owned by a caller instead of acquisition")

    print("battery refresh timestamp contract tests passed")


if __name__ == "__main__":
    main()
