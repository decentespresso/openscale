import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE_HEADER = ROOT / "include" / "ble.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"


def function_body(text, name):
    match = re.search(rf"\b\w+\s+{re.escape(name)}\([^;{{}}]*\)\s*{{", text)
    if match is None:
        raise AssertionError(f"missing function: {name}")
    opening = text.index("{", match.start())
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def assert_contains(text, *snippets):
    for snippet in snippets:
        if snippet not in text:
            raise AssertionError(f"missing snippet: {snippet}")


def main():
    ble = BLE_HEADER.read_text(encoding="utf-8")
    parameter = PARAMETER_HEADER.read_text(encoding="utf-8")
    hds = HDS_SOURCE.read_text(encoding="utf-8")

    assert_contains(parameter, "volatile uint16_t bleVoltageResponsesPending = 0;")
    assert_contains(function_body(ble, "resetBleFff4StateLocked"), "bleVoltageResponsesPending = 0;")

    sink = function_body(ble, "sendVoltage")
    assert_contains(sink, "queueBleVoltageResponse();")
    if "sendBleVoltage();" in sink:
        raise AssertionError("BLE callback sends the voltage notification directly")

    queue = function_body(ble, "queueBleVoltageResponse")
    assert_contains(
        queue,
        "portENTER_CRITICAL(&bleFff4Mux)",
        "bleVoltageResponsesPending != UINT16_MAX",
        "bleVoltageResponsesPending = bleVoltageResponsesPending + 1",
        "portEXIT_CRITICAL(&bleFff4Mux)",
    )

    process = function_body(ble, "processBleVoltageResponse")
    assert_contains(
        process,
        "portENTER_CRITICAL(&bleFff4Mux)",
        "bleVoltageResponsesPending = bleVoltageResponsesPending - 1",
        "portEXIT_CRITICAL(&bleFff4Mux)",
        "sendBleVoltage();",
    )
    if process.index("portEXIT_CRITICAL(&bleFff4Mux)") > process.index("sendBleVoltage();"):
        raise AssertionError("voltage notification runs while holding the mailbox lock")

    loop = function_body(hds, "loop")
    assert_contains(loop, "processBleVoltageResponse();")
    if loop.index("processBleVoltageResponse();") > loop.index("sendBleWeight();"):
        raise AssertionError("voltage response is not drained before periodic notifications")

    print("BLE voltage mailbox contract tests passed")


if __name__ == "__main__":
    main()
