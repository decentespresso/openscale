#!/usr/bin/env python3
import math
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "calibration_validation.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"
USBCOMM_HEADER = ROOT / "include" / "usbcomm.h"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"


def read(path):
    return path.read_text(encoding="utf-8")


def method_body(path, name):
    text = read(path)
    match = re.search(rf"\b\w+\s+{re.escape(name)}\(", text)
    if match is None:
        raise AssertionError(f"method not found: {name}")
    opening = text.index("{", match.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"method body not found: {name}")


def assert_ordered(body, snippets):
    cursor = 0
    for snippet in snippets:
        index = body.find(snippet, cursor)
        if index < 0:
            raise AssertionError(f"missing ordered snippet: {snippet}")
        cursor = index + len(snippet)


def const_float(name):
    text = read(HEADER)
    match = re.search(rf"const float {name} = ([0-9.]+)f;", text)
    if not match:
        raise AssertionError(f"missing {name}")
    return float(match.group(1))


def const_uint8(name):
    text = read(HEADER)
    match = re.search(rf"const uint8_t {name} = ([0-9]+);", text)
    if not match:
        raise AssertionError(f"missing {name}")
    return int(match.group(1))


def const_ulong(name):
    text = read(HEADER)
    match = re.search(rf"const unsigned long {name} = ([0-9]+);", text)
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
STABLE_READS_REQUIRED = const_uint8("CALIBRATION_STABLE_READS_REQUIRED")
CAPTURE_TIMEOUT_MS = const_ulong("CALIBRATION_CAPTURE_TIMEOUT_MS")
STABLE_HOLD_MS = const_ulong("CALIBRATION_STABLE_HOLD_MS")
ALLOW_NEGATIVE = True


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


def capture_after_plateau(samples, factor, elapsed_per_sample_ms):
    limit = stability_raw_limit(factor)
    stable_reads = 0
    previous = None
    elapsed_ms = 0
    last_capture = None
    stable_started_at = 0
    stable_min = None
    stable_max = None
    for current in samples:
        if elapsed_ms > CAPTURE_TIMEOUT_MS:
            break
        if previous is not None:
            spread = abs(current - previous)
            last_capture = {
                "raw": (current + previous) // 2,
                "spread": spread,
                "stable_reads": stable_reads,
            }
            if spread <= limit:
                if stable_reads == 0:
                    stable_started_at = elapsed_ms
                    stable_min = min(previous, current)
                    stable_max = max(previous, current)
                else:
                    stable_min = min(stable_min, current)
                    stable_max = max(stable_max, current)
                stable_reads += 1
            else:
                stable_reads = 0
                stable_started_at = 0
                stable_min = None
                stable_max = None
            last_capture["stable_reads"] = stable_reads
            stable_span = 0 if stable_min is None else stable_max - stable_min
            if (
                stable_reads >= STABLE_READS_REQUIRED
                and elapsed_ms - stable_started_at >= STABLE_HOLD_MS
                and stable_span <= limit
            ):
                return last_capture, last_capture
        previous = current
        elapsed_ms += elapsed_per_sample_ms
    return None, last_capture


class ScaleModel:
    def __init__(self):
        self.samples_in_use = 16
        self.valid_samples = 16
        self.smoothed_value = 8655800
        self.fresh_baseline = 8712314
        self.refresh_count = 0
        self.tare_offset = None

    def set_samples_in_use(self, samples_in_use):
        self.samples_in_use = samples_in_use
        self.valid_samples = 0
        self.smoothed_value = 8655900

    def refresh_dataset(self):
        self.valid_samples = self.samples_in_use
        self.smoothed_value = self.fresh_baseline
        self.refresh_count += 1
        return True

    def tare_no_delay(self):
        self.tare_offset = self.smoothed_value


def tare_when_adc_ready_model(scale):
    if scale.samples_in_use > 0 and scale.valid_samples < scale.samples_in_use:
        if not scale.refresh_dataset():
            return False
    scale.tare_no_delay()
    return True


def assert_contains(path, text):
    contents = read(path)
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def assert_not_contains(path, text):
    contents = read(path)
    if text in contents:
        raise AssertionError(f"{path.name} still contains {text}")


def test(name, actual, expected):
    if actual != expected:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main():
    test("stored positive factor", valid_value(1000.0), True)
    test("stored negative factor accepted", valid_value(-1000.0), True)
    test("stored huge factor rejected", valid_value(1500000.0), False)

    test("normal positive raw delta", validate_basics(50.0, 50000, 1000.0), "ok")
    test("normal positive verification", validate_verification(50.0, 49.9), "ok")
    test("negative raw delta accepted with negative factor", validate_basics(50.0, -50000, -1000.0), "ok")
    test("negative raw delta rejects positive factor", validate_basics(50.0, -50000, 1000.0), "factor_sign")
    test("positive raw delta rejects negative factor", validate_basics(50.0, 50000, -1000.0), "factor_sign")
    test("stale tare inverted verification", validate_verification(50.0, -49.9), "verify_inverted")
    test("zero raw delta", validate_basics(50.0, 0, 0.0), "raw_delta_too_small")
    test("too small raw delta", validate_basics(50.0, 20, 1.1), "raw_delta_too_small")
    test("huge factor rejected", validate_basics(50.0, 75000000, 1500000.0), "factor_too_large")
    test("unknown smart mass rejected", validate_basics(0.0, 50000, 1000.0), "unknown_mass")

    assert MIN_VALID_SAMPLES == 16
    assert STABLE_READS_REQUIRED == 3
    assert CAPTURE_TIMEOUT_MS == 8000
    assert STABLE_HOLD_MS == 1000
    assert stability_raw_limit(1000.0) == 500.0
    assert 600.0 > stability_raw_limit(1000.0)

    ramping_load = [
        8655802,
        8659844,
        8673000,
        8699000,
        8712309,
        8712311,
        8712314,
        8712313,
        8712312,
        8712314,
        8712313,
        8712313,
        8712312,
        8712314,
        8712313,
        8712313,
        8712312,
        8712314,
        8712313,
        8712313,
    ]
    load_capture, _ = capture_after_plateau(ramping_load, 1133.16, 100)
    assert load_capture["raw"] > 8712000
    assert load_capture["spread"] <= stability_raw_limit(1133.16)
    assert load_capture["stable_reads"] > STABLE_READS_REQUIRED

    slow_ramp = [8655800 + index * 400 for index in range(40)]
    slow_ramp_capture, slow_ramp_last = capture_after_plateau(slow_ramp, 1000.0, 100)
    assert slow_ramp_capture is None
    assert slow_ramp_last["spread"] <= stability_raw_limit(1000.0)

    scale_model = ScaleModel()
    scale_model.set_samples_in_use(4)
    assert tare_when_adc_ready_model(scale_model) is True
    assert scale_model.refresh_count == 1
    assert scale_model.valid_samples == scale_model.samples_in_use
    assert scale_model.tare_offset == scale_model.fresh_baseline

    never_stable, last_capture = capture_after_plateau(
        [8655800 + index * 1200 for index in range(100)],
        1133.16,
        100,
    )
    assert never_stable is None
    assert last_capture["spread"] > stability_raw_limit(1133.16)
    assert_contains(PARAMETER_HEADER, "bool refreshScaleDatasetAfterDiscontinuity")
    assert_contains(PARAMETER_HEADER, "void resetScaleOutputAfterAdcDiscontinuity")
    assert_contains(PARAMETER_HEADER, "bool tareScaleWhenAdcReady")
    assert_contains(PARAMETER_HEADER, "void clearPendingAutomaticTareState")
    assert_contains(PARAMETER_HEADER, "bool b_bootFreshTarePending")
    assert_contains(PARAMETER_HEADER, "const uint8_t BOOT_FRESH_TARE_SAMPLES = 4")
    assert_contains(PARAMETER_HEADER, "const unsigned long BOOT_FRESH_TARE_INPUT_SETTLE")
    assert_contains(HDS_SOURCE, "bool tareScaleWhenAdcReady")
    assert_contains(HDS_SOURCE, "bool startBootFreshTare")
    assert_contains(HDS_SOURCE, "bool processBootFreshTare")
    assert_contains(HDS_SOURCE, "bool isBootFreshTareInputSettled")
    assert_contains(HDS_SOURCE, "void consumeScaleTareStatus")
    assert_contains(HDS_SOURCE, "void clearPendingAutomaticTareState")
    assert_contains(USBCOMM_HEADER, "setScaleSamplesInUseWhenReady(samplesInUse, \"USB samples\")")
    assert_contains(WEBSOCKET_HEADER, "setScaleSamplesInUseWhenReady(samplesInUse, \"remote samples\")")
    assert_contains(HEADER.parent / "menu.h", "setScaleSamplesInUseWhenReady(1, \"calibration restore\")")
    assert_contains(HEADER.parent / "menu.h", "calibrationShowUsbWarning();")
    assert_not_contains(HEADER.parent / "menu.h", "Unplug scale")
    assert_not_contains(HEADER.parent / "menu.h", "calibrationFail(CAL_REJECT_UNPLUG_USB")
    assert_contains(HEADER.parent / "menu.h", "consumeScaleTareStatus();")
    assert_contains(HEADER.parent / "menu.h", "clearPendingAutomaticTareState();")
    assert read(HDS_SOURCE).count("scale.tareNoDelay()") == 1
    assert read(HDS_SOURCE).count("scale.tareFreshNoDelay()") == 1
    assert_not_contains(HDS_SOURCE, 'tareScaleWhenAdcReady("boot tare")')

    fast_tare = method_body(HDS_SOURCE, "tareScaleWhenAdcReady")
    assert "scale.tareNoDelay();" in fast_tare
    if "tareFresh" in fast_tare:
        raise AssertionError("runtime tare must stay fast")

    boot_start = method_body(HDS_SOURCE, "startBootFreshTare")
    assert_ordered(
        boot_start,
        [
            "scale.setSamplesInUse(BOOT_FRESH_TARE_SAMPLES);",
            "scale.tareFreshNoDelay();",
            "b_bootFreshTarePending = true;",
        ],
    )

    boot_process = method_body(HDS_SOURCE, "processBootFreshTare")
    assert_ordered(
        boot_process,
        [
            "if (!isBootFreshTareInputSettled())",
            "t_bootTare = millis();",
            "if (millis() - t_bootTare <= (unsigned long)i_bootTareDelay)",
            "return startBootFreshTare();",
            "if (scale.getTareStatus())",
            "restoreBootFreshTareSamples();",
            "resetScaleOutputAfterAdcDiscontinuity();",
            "b_bootTare = false;",
        ],
    )
    if "b_bootTare = false;" in boot_process.split("if (scale.getTareStatus())", 1)[0]:
        raise AssertionError("boot tare cleared before fresh tare completion")

    loop_body = method_body(HDS_SOURCE, "loop")
    assert "processBootFreshTare();" in loop_body
    assert 'tareScaleWhenAdcReady("button tare", true)' in loop_body
    assert 'tareScaleWhenAdcReady("remote tare", true)' in loop_body

    boot_gate = method_body(HDS_SOURCE, "isBootFreshTareInputSettled")
    for expected in (
            "b_menu || b_calibration || GPIO_power_on_with == BATTERY_CHARGING",
            "digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW",
            "millis() - t_menuExitTime <= BOOT_FRESH_TARE_INPUT_SETTLE"):
        if expected not in boot_gate:
            raise AssertionError(f"boot tare input gate missing {expected}")

    menu_header = HEADER.parent / "menu.h"
    for name in ("driftCompOff", "driftComp0050", "driftComp0075",
                 "driftComp0100", "driftComp0200"):
        body = method_body(menu_header, name)
        if "tare" in body.lower():
            raise AssertionError(f"{name} must not tare")

    assert_not_contains(USBCOMM_HEADER, "scale.setSamplesInUse")
    assert_not_contains(WEBSOCKET_HEADER, "scale.setSamplesInUse")
    print("calibration validation tests passed")


if __name__ == "__main__":
    main()
