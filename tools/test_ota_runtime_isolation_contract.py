from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def body_at(source, start):
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError("unterminated block")


def function_body(source, name):
    return body_at(source, source.index(f"{name}("))


def main():
    loop = function_body((ROOT / "src" / "hds.ino").read_text(encoding="utf-8"), "loop")
    setup = function_body((ROOT / "src" / "hds.ino").read_text(encoding="utf-8"), "setup")
    ble = (ROOT / "include" / "ble.h").read_text(encoding="utf-8")
    parameter = (ROOT / "include" / "parameter.h").read_text(encoding="utf-8")
    webserver = (ROOT / "include" / "webserver.h").read_text(encoding="utf-8")
    websocket = (ROOT / "include" / "websocket.h").read_text(encoding="utf-8")
    wifi_ota = (ROOT / "include" / "wifi_ota.h").read_text(encoding="utf-8")
    pending = function_body(
        websocket,
        "processWsPendingCmds",
    )

    pending_at = loop.index("processWsPendingCmds();")
    ota_at = loop.index("if (b_ota)")
    assert pending_at < ota_at < loop.index("processBleStatusResponse();")
    assert ota_at < loop.index("if (b_powerOff)")
    assert ota_at < loop.index("buttonCircle.check();")
    assert ota_at < loop.index("checkBattery();")
    assert loop.index("blePauseForOta()") < loop.index("ElegantOTA.loop();")
    assert "if (!otaTransportsStopped)" in loop
    assert loop.index("websocket.closeAll();") < loop.index("ElegantOTA.loop();")
    assert loop.index("wakeScaleFromSoftSleep(\"OTA wake\");") < loop.index("setOtaRuntimePaused(true);")
    assert loop.index("setEnergyPerformanceCritical(true);") < loop.index("setOtaRuntimePaused(true);")
    assert loop.index("if (!bleHasLiveClient())") < loop.index("setOtaRuntimePaused(true);")
    assert loop.index("processOtaDisplayUpdate();") < loop.index("processElegantOtaTimeout();")
    assert loop.index("setOtaRuntimePaused(false);") < loop.index("bleResumeAfterOta(")
    assert loop.index("bleResumeAfterOta(") < loop.index("processBleStatusResponse();")

    pending_at = setup.index("if (b_pendingOtaLittleFs)")
    assert pending_at < setup.index("b_ota = true;", pending_at)
    assert setup.index("b_ota = true;", pending_at) < setup.index("blePauseForOta();", pending_at)
    assert setup.index("blePauseForOta();", pending_at) < setup.index("pullOtaResumePendingLittleFs()", pending_at)
    pending_check_at = setup.index("const bool b_pendingOtaLittleFs = pullOtaHasPendingLittleFs();")
    assert pending_check_at < setup.index("ble_init();")
    assert "if (b_ble_enabled && !b_pendingOtaLittleFs)" in setup

    pause = function_body(ble, "blePauseForOta")
    resume = function_body(ble, "bleResumeAfterOta")
    assert pause.index("pAdvertising->stop();") < pause.index("pServer->disconnect")
    assert "return disconnectedClient;" in pause
    assert "b_ble_enabled" in resume
    assert "restoreDisplayAfterBleDisconnect();" not in resume
    assert ble.count("OTA active, advertising paused") == 2
    ble_write = function_body(ble, "onWrite")
    assert ble_write.index("if (b_ota) return;") < ble_write.index("getLength()")
    for disconnect_marker in (
        "void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc)",
        "void onDisconnect(BLEServer *pServer)",
    ):
        disconnect_at = ble.index(disconnect_marker)
        disconnect = body_at(ble, disconnect_at)
        assert disconnect.index("if (b_ota)") < disconnect.index("storageGetInt")
        assert disconnect.index("if (b_ota)") < disconnect.index("restoreDisplayAfterBleDisconnect")

    ota_gate = webserver.index("if (b_ota)")
    assert ota_gate < webserver.index('if (url == "/snapshot")')
    assert 'url == "/ota/upload"' in webserver
    assert 'url.startsWith("/ota/")' in webserver
    assert 'request->send(409, "text/plain", "pull OTA in progress")' in webserver
    assert 'request->getParam("mode")->value() == "fs"' in webserver
    assert '"filesystem OTA requires WiFi Update"' in webserver

    websocket_setup = function_body(websocket, "setupWebsocketEvents")
    assert websocket_setup.index("if (b_ota && (type == WS_EVT_CONNECT || type == WS_EVT_DATA))") < websocket_setup.index("if (type == WS_EVT_CONNECT)")

    start = function_body(wifi_ota, "onOTAStart")
    progress = function_body(wifi_ota, "onOTAProgress")
    timeout = function_body(wifi_ota, "processElegantOtaTimeout")
    ota_start = function_body(wifi_ota, "handleElegantOtaStart")
    ota_upload = function_body(wifi_ota, "handleElegantOtaUpload")
    ota_complete = function_body(wifi_ota, "completeElegantOtaUpload")
    ota_setup = function_body(wifi_ota, "wifiOta")
    assert start.index("recordElegantOtaActivity(millis());") < start.index("b_ota = true;")
    assert "recordElegantOtaActivity(ota_progress_millis);" in progress
    assert "millis() - ota_progress_millis >= OTA_PROGRESS_INTERVAL_MS" in progress
    assert "OTA_PROGRESS_INTERVAL_MS = 500" in wifi_ota
    assert "b_pullOtaRunning" in timeout
    assert "now - activityAt < OTA_ACTIVITY_TIMEOUT_MS" in timeout
    assert "remoteQueueOtaResetAt(now);" in timeout
    assert "volatile unsigned long otaActivityAt = 0;" in parameter
    assert "volatile bool otaRuntimePaused = false;" in parameter
    assert "bool elegantOtaUploadClaimed = false;" in parameter
    assert "OTA_RUNTIME_PAUSE_TIMEOUT_MS = 2000" in parameter
    assert ota_start.index("otaDispatchMutex") < ota_start.index("b_pullOtaRunning || b_ota")
    assert ota_start.index("b_pullOtaRunning || b_ota") < ota_start.index("otaRuntimeIsPaused()")
    assert ota_start.index("otaRuntimeIsPaused()") < ota_start.index("Update.begin")
    assert ota_start.index("Update.begin") < ota_start.index("Update.setMD5")
    assert ota_upload.index("otaDispatchMutex") < ota_upload.index("b_pullOtaRunning || !b_ota")
    assert ota_upload.index("otaRuntimeIsPaused()") < ota_upload.index("OTA_UPLOAD_OWNER_ATTRIBUTE")
    assert ota_upload.index("Update.isRunning()") < ota_upload.index("OTA_UPLOAD_OWNER_ATTRIBUTE")
    assert ota_upload.index("b_pullOtaRunning || !b_ota") < ota_upload.index("Update.write")
    assert ota_upload.index("b_pullOtaRunning || !b_ota") < ota_upload.index("Update.end(true)")
    assert "std::try_to_lock" in ota_start
    assert "std::try_to_lock" in ota_upload
    assert "std::try_to_lock" in ota_complete
    assert "OTA_UPLOAD_REJECTED_ATTRIBUTE" in ota_upload
    assert "OTA_UPLOAD_REJECTED_ATTRIBUTE" in ota_complete
    assert "OTA_UPLOAD_OWNER_ATTRIBUTE" in ota_upload
    assert "OTA_UPLOAD_OWNER_ATTRIBUTE" in ota_complete
    assert "if (elegantOtaUploadClaimed)" in ota_upload
    assert "elegantOtaUploadClaimed = true;" in ota_upload
    assert ota_upload.index("Update.end(true)") < ota_upload.index("OTA_UPLOAD_FINISHED_ATTRIBUTE")
    assert "request->getAttribute(OTA_UPLOAD_FINISHED_ATTRIBUTE, false)" in ota_complete
    assert "if (!success && Update.isRunning())" in ota_complete
    assert "elegantOtaUploadClaimed = false;" in ota_complete
    assert ota_setup.index('server.on("/ota/upload"') < ota_setup.index("ElegantOTA.begin(&server)")

    assert "b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask" in pending
    assert "wsPendingMask &= ~mask;" in pending
    update_at = pending.index("if (mask & WSP_WIFI_UPDATE)")
    assert update_at < pending.index("if (mask & WSP_DISPLAY_ON)")
    assert "wifiUpdate(otaTarget);" in pending
    assert pending.count("remoteRestoreDeferredPendingLocked(") == 2
    assert "remoteQueuePending(deferredMask);" not in pending

    restore = function_body(
        websocket,
        "remoteRestoreDeferredPendingLocked",
    )
    for group in (
        "WSP_DISPLAY_ON | WSP_DISPLAY_OFF",
        "WSP_LOWPWR_ON | WSP_LOWPWR_OFF",
        "WSP_SLEEP_ON | WSP_SLEEP_OFF",
        "WSP_TIMER_START | WSP_TIMER_STOP | WSP_TIMER_ZERO",
    ):
        assert group in restore
    assert "if (wsPendingMask & group)" in restore
    assert "deferredMask &= ~group;" in restore

    print("OTA runtime isolation contract tests passed")


if __name__ == "__main__":
    main()
