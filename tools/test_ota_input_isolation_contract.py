from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
USB_HEADER = ROOT / "include" / "usbcomm.h"


def extract_block(source, opener):
    start = source.find(opener)
    if start < 0:
        raise AssertionError(f"hds.ino missing {opener}")
    brace = source.find("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"hds.ino has unterminated block {opener}")


def require_all(source, markers, context):
    for marker in markers:
        if marker not in source:
            raise AssertionError(f"{context} missing {marker}")


def main():
    contents = HDS_SOURCE.read_text(encoding="utf-8")
    loop_start = contents.index("void loop() {")
    loop_end = contents.index("void chargingOLED(", loop_start)
    loop = contents[loop_start:loop_end]

    serial_input = extract_block(loop, "if (!b_ota && Serial.available()) {")
    require_all(
        serial_input,
        [
            "usbCallbacks.onStream(data, len);",
        ],
        "serial input gate",
    )
    poll_input = extract_block(loop, "if (!b_ota) {")
    require_all(
        poll_input,
        [
            "usbCallbacks.poll();",
        ],
        "USB timeout gate",
    )
    button_input = extract_block(
        loop,
        "if (!b_ota\n      && !buttonChecksSuppressedUntilRelease()",
    )
    require_all(
        button_input,
        [
            "buttonCircle.check();",
            "buttonSquare.check();",
        ],
        "button input gate",
    )

    gate_at = loop.index("if (!b_ota && Serial.available()) {")
    if loop.index("processWsPendingCmds();") >= gate_at:
        raise AssertionError("pending reset routing must run during OTA")
    if loop.index("power_off(15);") >= gate_at:
        raise AssertionError("low-battery and inactivity handling must precede input gating")

    ota_service = extract_block(loop, "if (b_ota) {")
    require_all(
        ota_service,
        ["ElegantOTA.loop();", "processOtaDisplayUpdate();", "return;"],
        "OTA service gate",
    )
    usb = USB_HEADER.read_text(encoding="utf-8")
    usb_buffer = extract_block(usb, "void processUsbRxBuffer(bool allowTimeout) {")
    parser_loop = extract_block(usb_buffer, "while (usbRxLen > 0) {")
    if parser_loop.index("if (b_ota) return;") > parser_loop.index("bool timedOut"):
        raise AssertionError("USB parser checks OTA state after starting command dispatch")

    print("OTA input isolation contract tests passed")


if __name__ == "__main__":
    main()
