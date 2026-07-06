from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
BLE_HEADER = ROOT / "include" / "ble.h"
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


def assert_contains(path, text):
    if text not in read(path):
        raise AssertionError(f"{path} missing {text!r}")


def assert_ordered(body, snippets):
    cursor = 0
    for snippet in snippets:
        index = body.find(snippet, cursor)
        if index < 0:
            raise AssertionError(f"missing ordered snippet: {snippet}")
        cursor = index + len(snippet)


def main():
    assert_contains(PARAMETER_HEADER, "bool wakeScaleFromSoftSleep")

    helper = method_body(HDS_SOURCE, "wakeScaleFromSoftSleep")
    assert_ordered(
        helper,
        [
            "digitalWrite(PWR_CTRL, HIGH);",
            "digitalWrite(ACC_PWR_CTRL, HIGH);",
            "scale.powerUp();",
            "refreshScaleDatasetAfterDiscontinuity(context)",
            "resetScaleOutputAfterAdcDiscontinuity();",
        ],
    )
    if ("tareScaleWhenAdcReady" in helper or "tareNoDelay" in helper or
            "tareFresh" in helper):
        raise AssertionError("soft wake must not tare automatically")

    button_handler = method_body(HDS_SOURCE, "aceButtonHandleEvent")
    assert_contains(HDS_SOURCE, 'wakeScaleFromSoftSleep("button soft wake")')
    if "digitalWrite(PWR_CTRL, HIGH);" in button_handler:
        raise AssertionError("button soft wake must use wakeScaleFromSoftSleep")

    usb_soft_off = method_body(USBCOMM_HEADER, "softSleepOff")
    assert_ordered(
        usb_soft_off,
        [
            "if (b_softSleep)",
            'wakeScaleFromSoftSleep("USB soft wake")',
            "u8g2.setPowerSave(0);",
            "b_u8g2Sleep = false;",
        ],
    )
    if "digitalWrite(PWR_CTRL, HIGH);" in usb_soft_off:
        raise AssertionError("USB soft wake must use wakeScaleFromSoftSleep")

    ble_soft_off = method_body(BLE_HEADER, "softSleepOff")
    assert_ordered(
        ble_soft_off,
        [
            "bool wasSoftSleep = b_softSleep;",
            "b_softSleep = false;",
            "if (wasSoftSleep)",
            "remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);",
            "remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);",
        ],
    )

    ws_handler = read(WEBSOCKET_HEADER)
    assert_ordered(
        ws_handler,
        [
            'if (action == "off" || action == "wake")',
            "bool wasSoftSleep = b_softSleep;",
            "if (wasSoftSleep)",
            "wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);",
            "wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);",
        ],
    )

    ws_pending = method_body(WEBSOCKET_HEADER, "processWsPendingCmds")
    assert_ordered(ws_pending, ['wakeScaleFromSoftSleep("remote soft wake")'])
    sleep_off_index = ws_pending.index("if (mask & WSP_SLEEP_OFF)")
    sleep_on_index = ws_pending.index("if (mask & WSP_SLEEP_ON)")
    sleep_off_body = ws_pending[sleep_off_index:sleep_on_index]
    if "digitalWrite(PWR_CTRL, HIGH);" in sleep_off_body:
        raise AssertionError("remote soft wake must use wakeScaleFromSoftSleep")

    print("soft sleep ADS wake tests passed")


if __name__ == "__main__":
    main()
