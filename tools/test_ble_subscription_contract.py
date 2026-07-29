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


class Mailbox:
    def __init__(self):
        self.connection = 7
        self.subscription = 0xFFFF
        self.pending = False
        self.requested_at = 0
        self.generation = 1
        self.retiring_generation = 0

    def queue(self, now):
        self.requested_at = now
        self.pending = True

    def subscribe(self):
        self.subscription = self.connection

    def begin_process(self, now):
        if not self.pending:
            return "wait"
        if self.subscription == self.connection:
            self.pending = False
            return "send"
        if now - self.requested_at < 2000:
            return "wait"
        self.pending = False
        self.retiring_generation = self.generation
        return "retire"

    def finish_retire(self):
        if self.generation != self.retiring_generation:
            return "wait"
        if self.pending:
            return "wait"
        if self.subscription == self.connection:
            return "send"
        return "disconnect"


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
        "volatile uint32_t bleFff4ConnectionGeneration = 0;",
        "portMUX_TYPE bleFff4Mux = portMUX_INITIALIZER_UNLOCKED;",
    )

    gate = function_body(ble, "bleCanNotifyCurrent")
    assert_contains(gate, "bleHasLiveClient()", "portENTER_CRITICAL(&bleFff4Mux)")
    for name in OUTBOUND_FUNCTIONS:
        body = function_body(ble, name)
        assert_contains(body, "if (!bleCanNotifyCurrent()) return;", "pReadCharacteristic->notify();")
    if ble.count("pReadCharacteristic->notify();") != len(OUTBOUND_FUNCTIONS):
        raise AssertionError("outbound FFF4 notification bypasses the shared gate")

    subscribe = function_body(ble, "onSubscribe")
    assert_contains(subscribe, "portENTER_CRITICAL(&bleFff4Mux)", "bleFff4SubscriptionHandle = nextHandle;")
    assert_contains(ble, "pReadCharacteristic->setCallbacks(new Fff4Callbacks());")

    for name in ("displayOff", "displayOn"):
        body = function_body(ble, name)
        assert_contains(body, "queueBleStatusResponse();")
        if "sendBleLedResponse();" in body:
            raise AssertionError(f"{name} sends status from the BLE callback")

    pending = function_body(ble, "processBleStatusResponse")
    assert_contains(
        pending,
        "if (bleStatusResponsePending)",
        "portENTER_CRITICAL(&bleFff4Mux)",
        "portEXIT_CRITICAL(&bleFff4Mux)",
        "sendBleLedResponse();",
        "now - bleStatusRequestAt >= BLE_STATUS_RESPONSE_TIMEOUT",
        "bleFff4ConnectionGeneration != connectionGeneration",
        "bleStatusResponsePending",
        "pServer->disconnect(currentConnId, 0x13);",
    )
    if pending.index("portEXIT_CRITICAL(&bleFff4Mux)") > pending.index("sendBleLedResponse();"):
        raise AssertionError("status notify runs while holding the FFF4 mailbox lock")
    if pending.rindex("portEXIT_CRITICAL(&bleFff4Mux)") > pending.index("pServer->disconnect(currentConnId, 0x13);"):
        raise AssertionError("BLE disconnect runs while holding the FFF4 mailbox lock")
    assert_contains(function_body(hds, "loop"), "processBleStatusResponse();")

    disconnect = function_body(ble, "onDisconnect")
    assert_contains(disconnect, "clearBleFff4Connection(desc->conn_handle)")

    status = function_body(ble, "onStatus")
    assert_contains(status, "bleNotifyFailureLogged", "bleNotifyFailureLogged = true;")

    mailbox = Mailbox()
    mailbox.queue(0)
    if mailbox.begin_process(2000) != "retire":
        raise AssertionError("status timeout did not enter retirement")
    mailbox.subscribe()
    if mailbox.finish_retire() != "send":
        raise AssertionError("subscription at the timeout boundary requested a disconnect")

    mailbox = Mailbox()
    mailbox.queue(0)
    if mailbox.begin_process(2000) != "retire":
        raise AssertionError("status timeout did not enter retirement")
    mailbox.queue(2000)
    if mailbox.finish_retire() != "wait" or not mailbox.pending:
        raise AssertionError("a newer status request was retired with the older timeout")

    mailbox = Mailbox()
    mailbox.queue(0)
    if mailbox.begin_process(2000) != "retire":
        raise AssertionError("status timeout did not enter retirement")
    mailbox.generation += 1
    if mailbox.finish_retire() != "wait":
        raise AssertionError("a reused connection handle inherited an older timeout")
    print("BLE subscription contract tests passed")


if __name__ == "__main__":
    main()
