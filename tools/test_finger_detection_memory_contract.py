#!/usr/bin/env python3
import random
from pathlib import Path


HEADER = Path(__file__).resolve().parents[1] / "include" / "finger_detection.h"
TOTAL_SAMPLES = 50
UINT32_MASK = (1 << 32) - 1


def elapsed(end, start):
    return (end - start) & UINT32_MASK


def legacy_metrics(samples, release_index, press_start, release_time, last_sample_time):
    peak_weight = samples[0]
    peak_index = 0
    for index, weight in enumerate(samples):
        if weight > peak_weight:
            peak_weight = weight
            peak_index = index
    clamped_release_index = min(release_index, len(samples) - 1)
    press_sample_time = release_time if release_index < len(samples) else last_sample_time
    final_sample_time = release_time if release_index == len(samples) - 1 else last_sample_time
    total_duration = elapsed(final_sample_time, press_start)
    return (
        len(samples),
        samples[0],
        samples[-1],
        peak_weight,
        peak_index,
        clamped_release_index,
        elapsed(press_sample_time, press_start),
        total_duration,
        total_duration / (len(samples) - 1) if len(samples) > 1 else 0.0,
    )


def running_metrics(samples, release_index, press_start, release_time, last_sample_time):
    start_weight = 0.0
    last_weight = 0.0
    peak_weight = 0.0
    peak_index = 0
    sample_count = 0
    for weight in samples:
        if sample_count == 0:
            start_weight = weight
            peak_weight = weight
            peak_index = 0
        elif weight > peak_weight:
            peak_weight = weight
            peak_index = sample_count
        last_weight = weight
        sample_count += 1
    clamped_release_index = min(release_index, sample_count - 1)
    press_sample_time = release_time if release_index < sample_count else last_sample_time
    final_sample_time = release_time if release_index == sample_count - 1 else last_sample_time
    total_duration = elapsed(final_sample_time, press_start)
    return (
        sample_count,
        start_weight,
        last_weight,
        peak_weight,
        peak_index,
        clamped_release_index,
        elapsed(press_sample_time, press_start),
        total_duration,
        total_duration / (sample_count - 1) if sample_count > 1 else 0.0,
    )


def classify(metrics):
    sample_count, start_weight, final_weight, peak_weight, _, _, press_duration, total_duration, _ = metrics
    if sample_count < 5:
        return sample_count >= 2 and abs(final_weight - start_weight) < 1.5
    peak_change = peak_weight - start_weight
    net_change = final_weight - start_weight
    recovery_ratio = (peak_weight - final_weight) / peak_change if peak_change > 0.1 else 0.0
    return (
        peak_change >= 3.0
        and abs(net_change) <= 2.0
        and recovery_ratio >= 0.85
        and press_duration <= 800
        and total_duration >= 300
    )


def assert_equivalent(samples, release_index, press_start, release_time, last_sample_time):
    legacy = legacy_metrics(samples, release_index, press_start, release_time, last_sample_time)
    running = running_metrics(samples, release_index, press_start, release_time, last_sample_time)
    assert running == legacy
    assert classify(running) == classify(legacy)


def main():
    source = HEADER.read_text(encoding="utf-8")
    for required in (
        "static_assert(TOTAL_SAMPLES < UINT8_MAX);",
        "static_assert(sizeof(ButtonPressData) <= 36);",
        "uint8_t sampleCount;",
        "uint8_t releaseIndex;",
        "uint8_t peakIndex;",
        "SamplingPhase phase;",
        "ButtonPressData circle_press_data = {0};",
    ):
        assert required in source
    for removed in ("struct PressSample", "samples[", "sample_index", "current_phase"):
        assert removed not in source
    assert source.count("addPressSample(*data, f_current_raw_value);") == 2
    assert source.count("addPressSample(*data, current_weight);") == 1
    assert source.count("data->peakWeight") == 2
    assert source.count("data->peakIndex") == 2

    traces = (
        ([0.0, 0.2], 1, 1000, 1040, 1040),
        ([0.0, 1.0, 3.2, 4.5, 2.0, 0.4], 3, 1000, 1250, 1510),
        ([10.0, 11.0, 13.0, 13.0, 11.5, 10.0], 3, 2000, 2400, 2600),
        ([-2.0, -1.0, 2.0, 1.0, -1.8], 2, 3000, 3300, 3600),
        ([0.0] * TOTAL_SAMPLES, TOTAL_SAMPLES, 4000, 4600, 4490),
        ([0.0, 4.0, 1.0, 5.0, 0.0], 2, 0xFFFFFFF0, 0x20, 0x140),
    )
    for trace in traces:
        assert_equivalent(*trace)

    random_source = random.Random(157)
    for _ in range(5000):
        sample_count = random_source.randint(1, TOTAL_SAMPLES)
        samples = [round(random_source.uniform(-20.0, 20.0), 3) for _ in range(sample_count)]
        release_index = random_source.choice((random_source.randrange(sample_count), sample_count - 1, TOTAL_SAMPLES))
        press_start = random_source.randrange(UINT32_MASK + 1)
        release_time = (press_start + random_source.randrange(1001)) & UINT32_MASK
        last_sample_time = (press_start + random_source.randrange(1201)) & UINT32_MASK
        assert_equivalent(samples, release_index, press_start, release_time, last_sample_time)

    print("finger detection running metrics tests passed")


if __name__ == "__main__":
    main()
