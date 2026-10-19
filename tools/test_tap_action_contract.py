from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, name: str) -> str:
    start = 0
    while True:
        start = source.index(f"{name}(", start)
        brace = source.find("{", start)
        semicolon = source.find(";", start)
        if brace != -1 and (semicolon == -1 or brace < semicolon):
            break
        start += len(name) + 1
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unterminated function: {name}")


def main() -> None:
    finger = (ROOT / "include" / "finger_detection.h").read_text(encoding="utf-8")
    tap = (ROOT / "include" / "tap_detection.h").read_text(encoding="utf-8")
    shared = function_body(finger, "runRecognizedButtonAction")
    recognized = function_body(finger, "isFingerPress")
    local_tap = function_body(tap, "runTapLocalAction")
    detector = function_body(tap, "tapDetectTick")
    parameters = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
    usb = (ROOT / "include" / "usbcomm.h").read_text(encoding="utf-8")
    sketch = (ROOT / "src" / "hds.ino").read_text(encoding="utf-8")
    weighing = function_body(sketch, "pureScale")

    assert "bool b_tapTraceEnabled = false;" in parameters
    assert 'inputString == "tapd on" || inputString == "tapd off"' in usb
    assert 'b_tapTraceEnabled = inputString == "tapd on";' in usb
    assert weighing.index("if (b_newDataReady)") < weighing.index("if (b_tapTraceEnabled)")
    assert 'Serial.printf("[TAPRAW] %lu %.3f\\n", t_lastScaleData, raw_weight);' in weighing

    assert "TAP_ACTION_DELAY_MS" not in tap
    assert "tapActionArmed" not in tap
    assert "bleClientLive && !b_btnFuncWhileConnected" in shared
    assert "sendUsbButton(buttonNumber, 1);" in shared
    assert "sendWebsocketButton(buttonNumber, 1);" in shared
    assert "sendBleButton(buttonNumber, 1);" in shared
    assert "runRecognizedButtonAction(button);" in recognized
    assert "now - t_menuExitTime <= 1000" in detector
    assert "scaleTimer();" in local_tap
    assert "b_weight_quick_zero = true;" in local_tap
    assert "b_tareByButton = true;" in local_tap
    assert "const bool timerRunning = stopWatch.isRunning();" in detector
    assert "(timerRunning && !b_tapTimerEnabled)" in detector
    assert "event == TapEvent::Double && b_tapTareEnabled && !timerRunning" in detector
    assert "event == TapEvent::Triple && b_tapTimerEnabled" in detector
    assert "grinderRuntime.state == GRINDER_STATE_GRINDING" in detector
    assert "grinderRuntime.state == GRINDER_STATE_STOPPING" in detector
    assert "tapDetector.reset(now, weight);" in detector
    assert "power_off(-1);" in detector
    assert "runTapLocalAction(tripleTapAction);" in detector
    assert "if (doubleTapAction || tripleTapAction) {" in detector
    assert "runRecognizedButtonAction" not in tap


if __name__ == "__main__":
    main()
