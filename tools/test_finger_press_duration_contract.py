import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG_HEADER = ROOT / "include" / "config.h"
FINGER_HEADER = ROOT / "include" / "finger_detection.h"


def define_value(text, name):
    match = re.search(rf"^#define {name} (\d+)", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing define: {name}")
    return int(match.group(1))


def main():
    config = CONFIG_HEADER.read_text(encoding="utf-8")
    finger = FINGER_HEADER.read_text(encoding="utf-8")
    long_press_delay = define_value(config, "LONGPRESS_DELAY")

    for name in (
        "CIRCLE_FINGER_PRESS_MAX_PRESS_TIME",
        "SQUARE_FINGER_PRESS_MAX_PRESS_TIME",
    ):
        if define_value(finger, name) >= long_press_delay:
            raise AssertionError(f"{name} overlaps the long-press threshold")

    predicate = re.search(r"bool is_finger_press =([^;]+);", finger)
    if predicate is None or "reasonable_press_time" not in predicate.group(1):
        raise AssertionError("finger press classification ignores maximum press time")

    print("finger press duration contract tests passed")


if __name__ == "__main__":
    main()
