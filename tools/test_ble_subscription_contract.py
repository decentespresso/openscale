#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE_HEADER = ROOT / "include" / "ble.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"
OUTBOUND_FUNCTIONS = (
    "sendBleVoltage",
    "sendBleHeartBeat",
    "sendBleGyro",
    "sendBleWeight",
    "sendBleButton",
    "sendBlePowerOff",
    "sendBleLedResponse",
    "sendAdsDebugInfoBLE",
)


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

    assert_contains(
        parameter,
        "volatile uint16_t bleFff4SubscriptionHandle = 0xFFFF;",
        "volatile bool bleStatusResponsePending = false;",
        "volatile unsigned long bleStatusRequestAt = 0;",
        "volatile bool bleNotifyFailureLogged = false;",
    )

    gate = function_body(ble, "bleCanNotifyCurrent")
    assert_contains(gate, "bleHasLiveClient()", "bleFff4SubscriptionHandle == connId")
    for name in OUTBOUND_FUNCTIONS:
        body = function_body(ble, name)
        assert_contains(body, "if (!bleCanNotifyCurrent()) return;", "pReadCharacteristic->notify();")
    if ble.count("pReadCharacteristic->notify();") != len(OUTBOUND_FUNCTIONS):
        raise AssertionError("outbound FFF4 notification bypasses the shared gate")

    subscribe = function_body(ble, "onSubscribe")
    assert_contains(subscribe, "desc->conn_handle != connId", "bleFff4SubscriptionHandle = nextHandle;")
    assert_contains(ble, "pReadCharacteristic->setCallbacks(new Fff4Callbacks());")

    for name in ("displayOff", "displayOn"):
        body = function_body(ble, name)
        assert_contains(body, "queueBleStatusResponse();")
        if "sendBleLedResponse();" in body:
            raise AssertionError(f"{name} sends status from the BLE callback")

    pending = function_body(ble, "processBleStatusResponse")
    assert_contains(
        pending,
        "if (!bleStatusResponsePending) return;",
        "if (bleCanNotifyCurrent())",
        "sendBleLedResponse();",
        "millis() - bleStatusRequestAt < BLE_STATUS_RESPONSE_TIMEOUT",
        "pServer->disconnect(currentConnId, 0x13);",
    )
    assert_contains(function_body(hds, "loop"), "processBleStatusResponse();")

    disconnect = function_body(ble, "onDisconnect")
    if disconnect.index("desc->conn_handle != connId") > disconnect.index("resetBleFff4State(0xFFFF)"):
        raise AssertionError("stale disconnect can clear the current FFF4 subscription")

    status = function_body(ble, "onStatus")
    assert_contains(status, "bleNotifyFailureLogged", "bleNotifyFailureLogged = true;")
    print("BLE subscription contract tests passed")


if __name__ == "__main__":
    main()
