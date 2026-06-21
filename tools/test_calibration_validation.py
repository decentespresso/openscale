#!/usr/bin/env python3
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "calibration_validation.h"


def const_float(name):
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(rf"const float {name} = ([0-9.]+)f;", text)
    if not match:
        raise AssertionError(f"missing {name}")
    return float(match.group(1))


def const_uint8(name):
    text = HEADER.read_text(encoding="utf-8")
    match = re.search(rf"const uint8_t {name} = ([0-9]+);", text)
    if not match:
        raise AssertionError(f"missing {name}")
    return int(match.group(1))


MIN_ABS = const_float("CALIBRATION_VALUE_MIN_ABS")
MAX_ABS = const_float("CALIBRATION_VALUE_MAX_ABS")
KNOWN_MIN = const_float("CALIBRATION_KNOWN_MASS_MIN_G")
VERIFY_MIN_TOLERANCE = const_float("CALIBRATION_VERIFY_MIN_TOLERANCE_G")
VERIFY_RATIO = const_float("CALIBRATION_VERIFY_TOLERANCE_RATIO")
STABILITY_MAX_G = const_float("CALIBRATION_STABILITY_MAX_G")
MIN_VALID_SAMPLES = const_uint8("CALIBRATION_MIN_VALID_SAMPLES")
ALLOW_NEGATIVE = False


def sign_allowed(value):
    return value != 0.0 if ALLOW_NEGATIVE else value > 0.0


def valid_value(value):
    magnitude = abs(value)
    return (
        math.isfinite(value)
        and sign_allowed(value)
        and magnitude >= MIN_ABS
        and magnitude <= MAX_ABS
    )


def verify_tolerance(known_mass):
    return max(abs(known_mass) * VERIFY_RATIO, VERIFY_MIN_TOLERANCE)


def stability_raw_limit(current_factor):
    magnitude = abs(current_factor)
    if not math.isfinite(magnitude) or magnitude < MIN_ABS:
        magnitude = 1000.0
    return max(magnitude * STABILITY_MAX_G, 50.0)


def validate_basics(known_mass, raw_delta, candidate):
    if not math.isfinite(known_mass) or known_mass < KNOWN_MIN:
        return "unknown_mass"
    if abs(raw_delta) < known_mass * MIN_ABS:
        return "raw_delta_too_small"
    if not math.isfinite(candidate):
        return "factor_near_zero"
    magnitude = abs(candidate)
    if magnitude < MIN_ABS:
        return "factor_near_zero"
    if magnitude > MAX_ABS:
        return "factor_too_large"
    if ALLOW_NEGATIVE:
        if (candidate > 0 and raw_delta <= 0) or (candidate < 0 and raw_delta >= 0):
            return "factor_sign"
    elif candidate <= 0 or raw_delta <= 0:
        return "factor_sign"
    return "ok"


def validate_verification(known_mass, verified_weight):
    if not math.isfinite(verified_weight) or verified_weight <= 0:
        return "verify_inverted"
    if abs(verified_weight - known_mass) > verify_tolerance(known_mass):
        return "verify_tolerance"
    return "ok"


def test(name, actual, expected):
    if actual != expected:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main():
    test("stored positive factor", valid_value(1000.0), True)
    test("stored negative factor rejected by default", valid_value(-1000.0), False)
    test("stored huge factor rejected", valid_value(1500000.0), False)

    test("normal positive raw delta", validate_basics(50.0, 50000, 1000.0), "ok")
    test("normal positive verification", validate_verification(50.0, 49.9), "ok")
    test("negative raw delta rejected by default", validate_basics(50.0, -50000, -1000.0), "factor_sign")
    test("stale tare inverted verification", validate_verification(50.0, -49.9), "verify_inverted")
    test("zero raw delta", validate_basics(50.0, 0, 0.0), "raw_delta_too_small")
    test("too small raw delta", validate_basics(50.0, 20, 1.1), "raw_delta_too_small")
    test("huge factor rejected", validate_basics(50.0, 75000000, 1500000.0), "factor_too_large")
    test("unknown smart mass rejected", validate_basics(0.0, 50000, 1000.0), "unknown_mass")

    assert MIN_VALID_SAMPLES == 16
    assert stability_raw_limit(1000.0) == 500.0
    assert 600.0 > stability_raw_limit(1000.0)
    print("calibration validation tests passed")


if __name__ == "__main__":
    main()
