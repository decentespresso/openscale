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
    setup = function_body((ROOT / "src" / "hds.ino").read_text(encoding="utf-8"), "setup")
    ble = (ROOT / "include" / "ble.h").read_text(encoding="utf-8")
    webserver = (ROOT / "include" / "webserver.h").read_text(encoding="utf-8")
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
    assert loop.index("blePauseForOta();") < loop.index("ElegantOTA.loop();")
    assert loop.index("websocket.closeAll();") < loop.index("ElegantOTA.loop();")
    assert loop.index("bleResumeAfterOta();") < loop.index("processBleStatusResponse();")

    pending_at = setup.index("if (b_pendingOtaLittleFs)")
    assert pending_at < setup.index("b_ota = true;", pending_at)
    assert setup.index("b_ota = true;", pending_at) < setup.index("blePauseForOta();", pending_at)
    assert setup.index("blePauseForOta();", pending_at) < setup.index("pullOtaResumePendingLittleFs()", pending_at)

    pause = function_body(ble, "blePauseForOta")
    resume = function_body(ble, "bleResumeAfterOta")
    assert pause.index("pAdvertising->stop();") < pause.index("pServer->disconnect")
    assert "b_ble_enabled" in resume
    assert ble.count("OTA active, advertising paused") == 2

    ota_gate = webserver.index("if (b_ota)")
    assert ota_gate < webserver.index('if (url == "/snapshot")')
    assert 'url == "/ota/upload"' in webserver
    assert 'url.startsWith("/ota/")' in webserver
    assert 'request->send(409, "text/plain", "pull OTA in progress")' in webserver
    assert 'request->getParam("mode")->value() == "fs"' in webserver
    assert '"filesystem OTA requires WiFi Update"' in webserver

    assert "b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask" in pending
    assert "wsPendingMask &= ~mask;" in pending
    update_at = pending.index("if (mask & WSP_WIFI_UPDATE)")
    assert update_at < pending.index("if (mask & WSP_DISPLAY_ON)")
    assert "remoteQueuePending(deferredMask);" in pending

    print("OTA runtime isolation contract tests passed")


if __name__ == "__main__":
    main()
