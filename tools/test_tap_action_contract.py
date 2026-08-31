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
    detector = function_body(tap, "tapDetectTick")

    assert "bleClientLive && !b_btnFuncWhileConnected" in shared
    assert "sendUsbButton(buttonNumber, 1);" in shared
    assert "sendWebsocketButton(buttonNumber, 1);" in shared
    assert "sendBleButton(buttonNumber, 1);" in shared
    assert "runRecognizedButtonAction(button);" in recognized
    assert "now - t_menuExitTime <= 1000" in detector
    assert "tapDetector.reset(now, weight);" in detector
    assert "runRecognizedButtonAction(tapTripleArmed ? BUTTON_SQUARE : BUTTON_CIRCLE);" in detector


if __name__ == "__main__":
    main()
