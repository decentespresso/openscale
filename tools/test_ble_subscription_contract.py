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
        self.pending = 0
        self.voltage_pending = 0
        self.requested_at = 0
        self.generation = 1
        self.retiring_generation = 0
        self.direct_notify_required = False

    def queue(self, now):
        self.requested_at = now
        self.pending += 1

    def subscribe(self):
        self.subscription = self.connection

    def queue_voltage(self):
        self.voltage_pending += 1

    def connect_normally(self, connection):
        self.connection = connection
        self.subscription = 0xFFFF
        self.pending = 0
        self.voltage_pending = 0
        self.direct_notify_required = False
        self.generation += 1

    def adopt_fallback(self, connection):
        self.connection = connection
        self.subscription = connection
        self.direct_notify_required = True
        self.generation += 1

    def disconnect(self):
        self.connection = 0xFFFF
        self.subscription = 0xFFFF
        self.pending = 0
        self.voltage_pending = 0
        self.direct_notify_required = False
        self.generation += 1

    def notify_path(self):
        return "raw" if self.direct_notify_required else "normal"

    def begin_process(self, now):
        if self.pending == 0:
            return "wait"
        if self.subscription == self.connection:
            self.pending -= 1
            return "send"
        if now - self.requested_at < 2000:
            return "wait"
        self.pending = 0
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


class FrameworkPeers:
    def __init__(self):
        self.peers = set()
        self.connected_count = 0

    def connect_with_failed_descriptor_lookup(self, connection):
        self.peers.add(connection)

    def remove_peer(self, connection):
        self.peers.discard(connection)

    def disconnect(self, connection):
        if connection in self.peers:
            self.peers.remove(connection)
            self.connected_count = (self.connected_count - 1) & 0xFFFFFFFF


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
        "volatile uint16_t bleStatusResponsesPending = 0;",
        "volatile unsigned long bleStatusRequestAt = 0;",
        "volatile bool bleNotifyFailureLogged = false;",
        "volatile bool bleDirectNotifyRequired = false;",
        "volatile uint32_t bleFff4ConnectionGeneration = 0;",
        "portMUX_TYPE bleFff4Mux = portMUX_INITIALIZER_UNLOCKED;",
    )

    gate = function_body(ble, "bleCanNotifyCurrent")
    assert_contains(gate, "bleHasLiveClient()", "portENTER_CRITICAL(&bleFff4Mux)")
    for name in OUTBOUND_FUNCTIONS:
        body = function_body(ble, name)
        assert_contains(body, "if (!bleCanNotifyCurrent()) return;", "bleNotifyReadPacket(")
    if ble.count("bleNotifyReadPacket(data,") != len(OUTBOUND_FUNCTIONS):
        raise AssertionError("outbound FFF4 notification bypasses the shared gate")
    sender = function_body(ble, "bleNotifyReadPacket")
    assert_contains(
        sender,
        "bleDirectNotifyRequired",
        "pReadCharacteristic->notify();",
        "ble_gatts_notify_custom(",
        "const int rc =",
        "if (rc != 0)",
        "logBleDirectNotifyFailure(",
    )
    if "getConnectedCount()" in sender:
        raise AssertionError("notification routing uses the framework counter as connection state")
    if ble.count("pReadCharacteristic->notify();") != 1:
        raise AssertionError("outbound FFF4 notification bypasses the shared send path")

    subscribe = function_body(ble, "onSubscribe")
    assert_contains(
        subscribe,
        "pServer->removePeerDevice(desc->conn_handle, false);",
        "adoptBleFff4ConnectionFromSubscription(desc->conn_handle);",
        "portENTER_CRITICAL(&bleFff4Mux)",
        "bleFff4SubscriptionHandle = nextHandle;",
    )
    if subscribe.index("removePeerDevice(") > subscribe.index("adoptBleFff4ConnectionFromSubscription("):
        raise AssertionError("fallback adoption leaves the framework phantom peer installed")
    assert_contains(ble, "pReadCharacteristic->setCallbacks(new Fff4Callbacks());")

    normal_connection = function_body(ble, "onConnect")
    assert_contains(normal_connection, "setBleFff4Connection(desc->conn_handle, 0xFFFF);")
    if "adoptBleFff4ConnectionFromSubscription" in normal_connection:
        raise AssertionError("normal connections use fallback adoption")

    adoption = function_body(ble, "adoptBleFff4ConnectionFromSubscription")
    assert_contains(adoption, "bleDirectNotifyRequired = true;", "bleFff4SubscriptionHandle = connectionHandle;")
    for cleared_state in ("resetBleFff4StateLocked", "bleStatusResponsesPending", "bleVoltageResponsesPending"):
        if cleared_state in adoption:
            raise AssertionError("fallback adoption clears pending responses")

    reset = function_body(ble, "resetBleFff4StateLocked")
    assert_contains(
        reset,
        "bleStatusResponsesPending = 0;",
        "bleVoltageResponsesPending = 0;",
        "bleDirectNotifyRequired = false;",
    )

    live_client = function_body(ble, "bleHasLiveClient")
    if "getConnectedCount()" in live_client:
        raise AssertionError("live-client state trusts the framework counter")

    for name in ("displayOff", "displayOn"):
        body = function_body(ble, name)
        assert_contains(body, "queueBleStatusResponse();")
        if "sendBleLedResponse();" in body:
            raise AssertionError(f"{name} sends status from the BLE callback")

    pending = function_body(ble, "processBleStatusResponse")
    assert_contains(
        pending,
        "if (bleStatusResponsesPending > 0)",
        "portENTER_CRITICAL(&bleFff4Mux)",
        "portEXIT_CRITICAL(&bleFff4Mux)",
        "sendBleLedResponse();",
        "now - bleStatusRequestAt >= BLE_STATUS_RESPONSE_TIMEOUT",
        "bleFff4ConnectionGeneration != connectionGeneration",
        "bleStatusResponsesPending",
        "pServer->disconnect(currentConnId, 0x13);",
    )
    if pending.index("portEXIT_CRITICAL(&bleFff4Mux)") > pending.index("sendBleLedResponse();"):
        raise AssertionError("status notify runs while holding the FFF4 mailbox lock")
    if pending.rindex("portEXIT_CRITICAL(&bleFff4Mux)") > pending.index("pServer->disconnect(currentConnId, 0x13);"):
        raise AssertionError("BLE disconnect runs while holding the FFF4 mailbox lock")
    loop = function_body(hds, "loop")
    assert_contains(loop, "processBleStatusResponse();")
    assert_contains(
        loop,
        "now - t_heartBeat > HEARTBEAT_TIMEOUT",
        "t_lastDisconnectAttempt == 0",
        "now - t_lastDisconnectAttempt >= HEARTBEAT_DISCONNECT_RETRY_INTERVAL",
        "disconnectBLE();",
    )
    if "t_heartBeat =" in loop:
        raise AssertionError("heartbeat timeout mutates heartbeat activity state")
    assert_contains(ble, "const unsigned long HEARTBEAT_DISCONNECT_RETRY_INTERVAL = 10000;")

    heartbeat_disconnect = function_body(ble, "disconnectBLE")
    assert_contains(
        heartbeat_disconnect,
        "if (!bleHasLiveClient()",
        "now - t_lastDisconnectAttempt < HEARTBEAT_TIMEOUT",
        "t_lastDisconnectAttempt = now;",
    )
    heartbeat_log = heartbeat_disconnect.index("***No heartbeat for 5 seconds. Disconnecting BLE...***")
    if heartbeat_disconnect[:heartbeat_log].count("return;") < 2:
        raise AssertionError("heartbeat disconnect log runs before retry throttling")

    for name in ("buttonCircle_DoubleClicked", "buttonSquare_DoubleClicked"):
        body = function_body(hds, name)
        assert_contains(body, "const bool bleClientLive = bleHasLiveClient();")
        if "deviceConnected" in body:
            raise AssertionError(f"{name} uses stale BLE callback state")

    disconnect = function_body(ble, "onDisconnect")
    assert_contains(disconnect, "clearBleFff4Connection(desc->conn_handle)")
    clear_connection = function_body(ble, "clearBleFff4Connection")
    assert_contains(clear_connection, "resetBleFff4StateLocked(0xFFFF);")

    status = function_body(ble, "onStatus")
    assert_contains(status, "bleNotifyFailureLogged", "bleNotifyFailureLogged = true;")

    mailbox = Mailbox()
    mailbox.queue(0)
    mailbox.queue(1)
    mailbox.subscribe()
    if mailbox.begin_process(1) != "send" or mailbox.pending != 1:
        raise AssertionError("queued status responses were coalesced")
    if mailbox.begin_process(1) != "send" or mailbox.pending != 0:
        raise AssertionError("queued status response was not drained")

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

    mailbox = Mailbox()
    mailbox.queue(0)
    mailbox.queue_voltage()
    mailbox.adopt_fallback(9)
    if mailbox.pending != 1 or mailbox.voltage_pending != 1:
        raise AssertionError("fallback adoption cleared pending responses")
    if mailbox.notify_path() != "raw":
        raise AssertionError("fallback connection did not use raw notifications")
    mailbox.disconnect()
    if mailbox.direct_notify_required or mailbox.connection != 0xFFFF:
        raise AssertionError("disconnect did not clear fallback state")

    mailbox = Mailbox()
    mailbox.queue(0)
    mailbox.queue_voltage()
    mailbox.connect_normally(9)
    if mailbox.pending or mailbox.voltage_pending:
        raise AssertionError("normal connection did not reset pending responses")
    if mailbox.notify_path() != "normal":
        raise AssertionError("normal connection did not use normal notifications")

    broken_framework = FrameworkPeers()
    broken_framework.connect_with_failed_descriptor_lookup(9)
    broken_framework.disconnect(9)
    if broken_framework.connected_count != 0xFFFFFFFF:
        raise AssertionError("framework model does not reproduce the counter underflow")

    fixed_framework = FrameworkPeers()
    fixed_framework.connect_with_failed_descriptor_lookup(9)
    fixed_framework.remove_peer(9)
    fixed_framework.disconnect(9)
    if fixed_framework.connected_count != 0 or fixed_framework.peers:
        raise AssertionError("fallback disconnect underflowed the framework connection count")
    print("BLE subscription contract tests passed")


if __name__ == "__main__":
    main()
