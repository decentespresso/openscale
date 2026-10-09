from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def main():
    routes = read("include/wifi_setup_routes.h")
    worker = read("src/wifi_setup_control.cpp")
    radio = read("src/wifi_setup.cpp")
    settings = read("src/wifi_settings.cpp")
    script = read("include/wifi_setup_page.h")
    index = read("plugins/default-web-apps/assets/index.html")
    globals_source = read("include/parameter.h")
    server = read("include/webserver.h")
    pull_ota = read("include/pull_ota.h")
    assert '"wifi": ("HDS_FEATURE_WIFI", ("webserver",)' in read("tools/configure_custom_build.py")
    assert "HDS_FEATURE_WIFI requires HDS_FEATURE_WEBSERVER" in read("include/hds_features.h")
    assert "wifi_config_server.h" not in read("src/hds.ino")
    for path in ("/setup/wifi", "/setup/wifi/status", "/setup/wifi/scan"):
        assert f'"{path}"' in routes, path
    assert "wifiSendJson(request, 202, response)" in routes
    assert 'response["restarting"] = false' in routes
    assert "remoteQueueResetAt" not in routes
    assert "saveCredentials" not in routes
    assert "wifiNormalizeCredentials" in routes
    assert "ssid.size() == 0 && pass.size() == 0" in routes
    assert "wifiRequestOriginAllowed(request)" in routes
    assert "origin == \"http://\" + host" in routes
    assert 'request->getHeader("Sec-Fetch-Site")->value() == "cross-site"' in routes
    assert 'response["pass"]' not in routes
    assert 'response["password"]' not in routes
    for field in ("wifiPendingRequest", "wifiSetupStatus", "wifiScanResult"):
        assert f"volatile " in globals_source[:globals_source.index(field)]
        assert field in worker
    assert "portENTER_CRITICAL(&wifiSetupMux)" in worker
    assert "wifiGotIpGeneration != wifiSetupRuntime.ipBaseline" in worker
    assert "WiFi.SSID() == ssid" in worker
    assert "connectionGeneration = wifiDisconnectGeneration" in worker
    assert "!wifiOperationReserved && !b_ota && !wifiWorkerStopRequested" in worker
    assert "memcpy((void *)&wifiPendingRequest, &empty, sizeof(empty))" in worker
    assert "request.command != WifiSetupCommand::Scan) wifiSetupRuntime.recoveryApAt = 0" in worker
    assert "!wifiSetupBusy() && millis() - wifiSetupRuntime.recoveryApAt >= 600000" in worker
    assert "wifiExecuteSwitchAction(wifiSetupRuntime.change.restore(now, wifiSetupRuntime.change.error))" in worker
    assert "scanned && !wifiScannedSsid(ssid.c_str(), ssid.size())" in routes
    assert 'object["scanned"].is<bool>()' in routes
    assert "pass.size(), credentials, !scanned" in routes
    assert 'if (url == "/ota/start")' in server
    assert "(wifiSetupBusy() || b_ota)" in server
    assert 'url == "/setup/wifi/scan" && request->method() == HTTP_POST' in server
    assert "if (!wifiReserveExternalOperation())" in server
    assert "if (!wifiReserveExternalOperation())" in pull_ota
    assert "wifiReleaseExternalOperation();" in pull_ota
    assert "if (action == WifiSwitchAction::Save)" in worker
    assert worker.index("if (action == WifiSwitchAction::Save)") < worker.index(
        "params.saveCredentials(wifiSetupRuntime.candidate.ssid"
    )
    assert "xTaskGetCurrentTaskHandle() != wifiWorkerTask" in radio
    assert "xSemaphoreTake(wifiRadioMutex, portMAX_DELAY)" in radio
    assert "wifiCancelSetup();" in radio
    assert "WiFi.setSleep(false)" not in radio
    assert "downSince == 0 && now - wifiSetupRuntime.stationStartedAt < 20000" in radio
    assert 'putString(wifiSSIDKey' not in settings
    assert 'putString(wifiPassKey' not in settings
    assert 'putBytes("credentials", &record, sizeof(record))' in settings
    assert 'memcmp(&record, &stored, sizeof(record)) == 0' in settings
    assert settings.index("memcmp(&record, &stored") < settings.index("ssid = newSsid")
    assert "preferences.clear()" not in settings
    assert "saved = ssidRemoved && passRemoved" in settings
    assert "!preferences.isKey(wifiSSIDKey) && !preferences.isKey(wifiPassKey)" in settings
    for element in ("wifi-form", "wifi-networks", "wifi-scan-button", "show-password",
                    "wifi-scan-progress", "wifi-reset-dialog"):
        assert f'id="{element}"' in script
        assert f'id="{element}"' in index
    assert "localStorage" not in script
    assert "innerHTML" not in script
    assert "Default_Ignorable_Code_Point" in script
    assert "data.operation_id !== id" in script
    assert "confirm(" not in script
    assert "resetDialog.showModal()" in script
    assert "resetDialog.returnValue === 'reset'" in script
    assert 'src="/setup/wifi.js"' in index
    assert 'src="shared/wifi-legacy.js" defer' in index
    assert "form.dataset.wifiSetup = 'verified'" in script
    assert read("plugins/default-web-apps/assets/shared/wifi-legacy.css").strip() == \
        script.split('R"css(', 1)[1].split(')css";', 1)[0].strip()
    resetSave = radio[radio.index("bool saveCredentials(const String &ssid, const String &pass) {"):]
    assert resetSave.index("params.saveCredentials(ssid, pass)") < resetSave.index("if (saved) wifiCancelSetup()")
    print("WiFi setup contracts passed")


if __name__ == "__main__":
    main()
