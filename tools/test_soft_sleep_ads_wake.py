from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
BLE_HEADER = ROOT / "include" / "ble.h"
USBCOMM_HEADER = ROOT / "include" / "usbcomm.h"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"
MENU_HEADER = ROOT / "include" / "menu.h"


def read(path):
    return path.read_text(encoding="utf-8")


def method_body(path, name):
    text = read(path)
    match = re.search(rf"\b\w+\s+{re.escape(name)}\([^)]*\)\s*\{{", text)
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


def energy_and_stock_branches(body):
    start = body.index("#if HDS_ENABLE_ENERGY_MENU")
    divider = body.index("#else", start)
    end = body.index("#endif", divider)
    return body[start:divider], body[divider:end]


def main():
    assert_contains(PARAMETER_HEADER, "bool wakeScaleFromSoftSleep")

    helper = method_body(HDS_SOURCE, "wakeScaleFromSoftSleep")
    assert_ordered(
        helper,
        [
            "digitalWrite(PWR_CTRL, HIGH);",
            "digitalWrite(ACC_PWR_CTRL, HIGH);",
            "scale.powerUp();",
            "b_softSleep = false;",
            "applyEnergyDisplayCommand(!energyRuntime.explicitDisplayOff);",
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
            "#if HDS_ENABLE_ENERGY_MENU",
            "else if (!energyRuntime.explicitDisplayOff)",
            "applyEnergyDisplayCommand(true);",
            "#else",
            "u8g2.setPowerSave(0);",
        ],
    )
    if "digitalWrite(PWR_CTRL, HIGH);" in usb_soft_off:
        raise AssertionError("USB soft wake must use wakeScaleFromSoftSleep")

    wifi_update = method_body(MENU_HEADER, "wifiUpdate")
    assert_ordered(
        wifi_update,
        [
            "if (b_softSleep)",
            'wakeScaleFromSoftSleep("WiFi OTA wake")',
            "pullOtaUpdate();",
        ],
    )

    ble_soft_on = method_body(BLE_HEADER, "softSleepOn")
    if "b_softSleep = true;" in ble_soft_on or "b_u8g2Sleep = true;" in ble_soft_on:
        raise AssertionError("BLE sleep publishes state before main-loop rail shutdown")
    assert_ordered(ble_soft_on, ["remoteReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);"])

    ble_soft_off = method_body(BLE_HEADER, "softSleepOff")
    ble_energy_wake, ble_stock_wake = energy_and_stock_branches(ble_soft_off)
    assert_ordered(
        ble_energy_wake,
        [
            "remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);",
        ],
    )
    if "b_softSleep = false;" in ble_energy_wake or "b_u8g2Sleep = false;" in ble_energy_wake:
        raise AssertionError("BLE wake publishes state before main-loop recovery")
    assert_ordered(
        ble_stock_wake,
        [
            "const bool wasSoftSleep = b_softSleep;",
            "b_softSleep = false;",
            "b_u8g2Sleep = false;",
            "if (wasSoftSleep)",
            "remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);",
            "else",
            "remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);",
        ],
    )

    ble_disconnect_display = method_body(BLE_HEADER, "restoreDisplayAfterBleDisconnect")
    assert_ordered(
        ble_disconnect_display,
        [
            "if (b_softSleep) return;",
            "b_u8g2Sleep = false;",
            "remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);",
        ],
    )
    if read(BLE_HEADER).count("restoreDisplayAfterBleDisconnect();") != 2:
        raise AssertionError("all BLE disconnect callbacks must preserve soft sleep")

    ws_handler = read(WEBSOCKET_HEADER)
    ws_wake_start = ws_handler.index('if (action == "off" || action == "wake")')
    ws_wake_end = ws_handler.index('sendWebsocketStatus(client, "ok");', ws_wake_start)
    ws_wake = ws_handler[ws_wake_start:ws_wake_end]
    ws_energy_wake, ws_stock_wake = energy_and_stock_branches(ws_wake)
    ws_sleep_start = ws_handler.index('if (action == "on")', ws_handler.index('if (command == "sleep"'))
    ws_sleep_end = ws_handler.index('sendWebsocketStatus(client, "ok");', ws_sleep_start)
    ws_sleep = ws_handler[ws_sleep_start:ws_sleep_end]
    if "b_softSleep = true;" in ws_sleep or "b_u8g2Sleep = true;" in ws_sleep:
        raise AssertionError("WebSocket sleep publishes state before main-loop rail shutdown")
    assert_ordered(ws_sleep, ["wsReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);"])
    assert_ordered(
        ws_energy_wake,
        [
            "wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);",
        ],
    )
    if "b_softSleep = false;" in ws_energy_wake or "b_u8g2Sleep = false;" in ws_energy_wake:
        raise AssertionError("WebSocket wake publishes state before main-loop recovery")
    assert_ordered(
        ws_stock_wake,
        [
            "const bool wasSoftSleep = b_softSleep;",
            "b_softSleep = false;",
            "b_u8g2Sleep = false;",
            "if (wasSoftSleep)",
            "wsReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);",
            "else",
            "wsReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);",
        ],
    )

    ws_pending = method_body(WEBSOCKET_HEADER, "processWsPendingCmds")
    assert_ordered(ws_pending, ['wakeScaleFromSoftSleep("remote soft wake")'])
    sleep_off_index = ws_pending.index("if (mask & WSP_SLEEP_OFF)")
    sleep_on_index = ws_pending.index("if (mask & WSP_SLEEP_ON)")
    sleep_off_body = ws_pending[sleep_off_index:sleep_on_index]
    assert_ordered(
        sleep_off_body,
        [
            "if (b_softSleep)",
            'wakeScaleFromSoftSleep("remote soft wake")',
            "u8g2.setPowerSave(0);",
            "b_u8g2Sleep = false;",
        ],
    )
    if "digitalWrite(PWR_CTRL, HIGH);" in sleep_off_body:
        raise AssertionError("remote soft wake must use wakeScaleFromSoftSleep")
    sleep_on_body = ws_pending[sleep_on_index:]
    assert_ordered(
        sleep_on_body,
        [
            "b_softSleep = true;",
            "b_u8g2Sleep = true;",
            "u8g2.setPowerSave(1);",
            "digitalWrite(PWR_CTRL, LOW);",
            "digitalWrite(ACC_PWR_CTRL, LOW);",
        ],
    )

    print("soft sleep ADS wake tests passed")


if __name__ == "__main__":
    main()
