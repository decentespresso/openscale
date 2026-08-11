from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE_HEADER = ROOT / "include" / "pressensor_ble.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"


class LinkModel:
    def __init__(self):
        self.connected_once = False
        self.generation = 0

    def subscribe(self):
        zero = not self.connected_once
        self.connected_once = True
        return zero

    def reconnect(self):
        self.generation += 1

    def accepts(self, generation):
        return generation == self.generation


def function_body(text, name):
    signature = text.index(name)
    opening = text.index("{", signature)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def require(text, *snippets):
    for snippet in snippets:
        if snippet not in text:
            raise AssertionError(f"missing contract: {snippet}")


def main():
    ble = BLE_HEADER.read_text(encoding="utf-8")
    parameter = PARAMETER_HEADER.read_text(encoding="utf-8")
    compact = " ".join(ble.split())

    require(
        parameter,
        "extern PressensorLink pressensorLink;",
        "extern portMUX_TYPE pressensorMux;",
        "extern volatile unsigned long pressensorNotifyAtShared;",
    )
    require(
        ble,
        "generation == pressensorLink.generation",
        "event->disconnect.conn.conn_handle == pressensorLink.connHandle",
        "event->notify_rx.conn_handle == pressensorLink.connHandle",
        "pressensorLink.generation = pressensorLink.generation + 1;",
    )
    if ble.count("portENTER_CRITICAL(&pressensorMux);") < 20:
        raise AssertionError("Pressensor callback state is not consistently protected")
    if "matchAnyDevice" in ble:
        raise AssertionError("an empty selection can still match arbitrary sensors")

    begin = function_body(ble, "pressensorLinkBegin")
    require(begin, "pressensorLink.targetMac[0] == 0 ? PRESSENSOR_LINK_OFF : PRESSENSOR_LINK_SCAN_WAIT")

    subscription = function_body(ble, "pressensorSubscriptionWritten")
    require(subscription, "generation == pressensorLink.generation", "error->status == 0", "pressensorLink.phaseDone = true;")
    require(
        compact,
        "ble_gattc_write_flat(handle, cccdHandle, enableNotify, sizeof(enableNotify), pressensorSubscriptionWritten, pressensorGenerationArg(generation))",
    )

    tick = function_body(ble, "pressensorLinkTick")
    require(
        tick,
        "zeroInitialConnection = !pressensorLink.connectedOnce;",
        "pressensorLink.connectedOnce = true;",
        "if (zeroInitialConnection)",
        "pressensorZeroNow();",
        "notifyAt == 0 && now - startedAt > PRESSENSOR_PHASE_TIMEOUT_MS",
        "notifyAt != 0 && now - notifyAt >= PRESSENSOR_STALE_MS",
        "pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);",
    )

    link = LinkModel()
    if not link.subscribe():
        raise AssertionError("first connection did not zero")
    link.reconnect()
    if link.subscribe():
        raise AssertionError("reconnect zeroed the sensor")
    old_generation = link.generation
    link.reconnect()
    if link.accepts(old_generation):
        raise AssertionError("late callback from an older generation was accepted")

    print("Pressensor BLE contract tests passed")


if __name__ == "__main__":
    main()
