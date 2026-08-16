#!/usr/bin/env python3
from pathlib import Path


HEADER = Path(__file__).resolve().parents[1] / "include" / "finger_detection.h"
UINT32_MASK = (1 << 32) - 1


def cached_timing(press_start, release_time, last_sample_time, release_index, sample_count):
    press_sample_time = release_time if release_index < sample_count else last_sample_time
    final_sample_time = release_time if release_index == sample_count - 1 else last_sample_time
    return (
        (press_sample_time - press_start) & UINT32_MASK,
        (final_sample_time - press_start) & UINT32_MASK,
    )


def main():
    source = HEADER.read_text(encoding="utf-8")
    for required in (
        "static_assert(TOTAL_SAMPLES < 255);",
        "static_assert(sizeof(PressSample) == 4);",
        "uint8_t release_index;",
        "data->release_index = TOTAL_SAMPLES;",
        "data->release_index = data->sample_index;",
    ):
        assert required in source
    for removed in (".timestamp", ".is_release_point", "if (data->release_time >", "if (data->last_sample_real_time >"):
        assert removed not in source

    timing_expressions = (
        "data->release_index < data->sample_index ? data->release_time : data->last_sample_real_time",
        "data->release_index == data->sample_index - 1 ? data->release_time : data->last_sample_real_time",
    )
    for expression in timing_expressions:
        assert source.count(expression) == 2
    assert source.count("int peak_index = 0;") == 2
    assert source.count("peak_index = i;") == 2
    assert source.count("if (release_index >= data->sample_index)") == 2

    scenarios = (
        ((1000, 1200, 1500, 20, 30), (200, 500)),
        ((1000, 1200, 1190, 20, 21), (200, 200)),
        ((1000, 1600, 1490, 50, 50), (490, 490)),
        ((0xFFFFFFF0, 0x10, 0x40, 3, 7), (32, 80)),
    )
    for inputs, expected in scenarios:
        assert cached_timing(*inputs) == expected

    print("finger detection memory contract tests passed")


if __name__ == "__main__":
    main()
