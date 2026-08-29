from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source, name):
    start = source.index(f"void {name}(")
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated function: {name}")


def main():
    loop = function_body((ROOT / "src" / "hds.ino").read_text(encoding="utf-8"), "loop")
    pending = function_body(
        (ROOT / "include" / "websocket.h").read_text(encoding="utf-8"),
        "processWsPendingCmds",
    )

    pending_at = loop.index("processWsPendingCmds();")
    ota_at = loop.index("if (b_ota)")
    assert pending_at < ota_at < loop.index("processBleStatusResponse();")
    assert ota_at < loop.index("if (b_powerOff)")
    assert ota_at < loop.index("buttonCircle.check();")
    assert ota_at < loop.index("checkBattery();")

    assert "b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask" in pending
    assert "wsPendingMask &= ~mask;" in pending
    update_at = pending.index("if (mask & WSP_WIFI_UPDATE)")
    assert update_at < pending.index("if (mask & WSP_DISPLAY_ON)")
    assert "remoteQueuePending(deferredMask);" in pending

    print("OTA runtime isolation contract tests passed")


if __name__ == "__main__":
    main()
