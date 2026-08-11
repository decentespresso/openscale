from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"


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

    normal_input = extract_block(loop, "if (!b_ota) {")
    require_all(
        normal_input,
        [
            "Serial.available()",
            "usbCallbacks.onStream(data, len);",
            "usbCallbacks.poll();",
            "buttonCircle.check();",
            "buttonSquare.check();",
        ],
        "normal input gate",
    )

    gate_at = loop.index("if (!b_ota) {")
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
    if loop.index("checkBattery();") >= loop.index("if (b_ota) {"):
        raise AssertionError("charging-state refresh must run before OTA servicing")

    print("OTA input isolation contract tests passed")


if __name__ == "__main__":
    main()
