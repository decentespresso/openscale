from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_OTA_HEADER = ROOT / "include" / "wifi_ota.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"
WIFI_SETUP_SOURCE = ROOT / "src" / "wifi_setup.cpp"
HDS_SOURCE = ROOT / "src" / "hds.ino"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"


def assert_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def assert_not_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text in contents:
        raise AssertionError(f"{path.name} contains {text}")


def assert_before(path, first, second):
    contents = path.read_text(encoding="utf-8")
    first_at = contents.find(first)
    second_at = contents.find(second)
    if first_at < 0:
        raise AssertionError(f"{path.name} missing {first}")
    if second_at < 0:
        raise AssertionError(f"{path.name} missing {second}")
    if first_at >= second_at:
        raise AssertionError(f"{path.name} must place {first} before {second}")


def assert_pending_sleep_ota_interleaving():
    pending_sleep = True
    soft_sleep = False
    rails_on = True
    ota_active = True

    if ota_active and soft_sleep:
        soft_sleep = False
        rails_on = True
    if pending_sleep:
        pending_sleep = False
        soft_sleep = True
        rails_on = False
    if ota_active and soft_sleep:
        soft_sleep = False
        rails_on = True

    if pending_sleep or soft_sleep or not rails_on:
        raise AssertionError("OTA wake leaves applied soft sleep or powered-off rails")


def assert_ordered(path, snippets):
    contents = path.read_text(encoding="utf-8")
    cursor = 0
    for snippet in snippets:
        index = contents.find(snippet, cursor)
        if index < 0:
            raise AssertionError(f"{path.name} missing ordered snippet: {snippet}")
        cursor = index + len(snippet)


def main():
    assert_pending_sleep_ota_interleaving()
    hds = HDS_SOURCE.read_text(encoding="utf-8")
    loop = hds[hds.index("void loop() {"):hds.index("void chargingOLED(")]
    if "if (b_ota || bleHasLiveClient()" not in loop:
        raise AssertionError("OTA does not suspend the auto-sleep countdown")
    ota_wake = 'if (b_ota && b_softSleep) {\n    wakeScaleFromSoftSleep("OTA wake");'
    if ota_wake not in loop:
        raise AssertionError("active OTA does not wake soft-sleep hardware")
    if loop.index("processWsPendingCmds();") > loop.index(ota_wake):
        raise AssertionError("pending sleep must be applied before OTA wake")
    if loop.index(ota_wake) > loop.index("if (!b_softSleep)"):
        raise AssertionError("OTA wake runs after the sleeping-loop gate")

    # ElegantOTA's own auto-reboot bypasses reset()'s mDNS withdrawal; the
    # restart must be queued through the main-loop reset mechanism instead.
    assert_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(false)")
    assert_not_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(true)")
    assert_contains(WIFI_OTA_HEADER, "remoteQueueResetAt(millis() + OTA_RESTART_DELAY_MS)")
    assert_ordered(
        WIFI_OTA_HEADER,
        [
            "void onOTAStart()",
            "portENTER_CRITICAL(&wsPendingMux);",
            "b_ota = true;",
            "portEXIT_CRITICAL(&wsPendingMux);",
        ],
    )

    # Pull OTA's success paths must queue the restart rather than calling
    # ESP.restart() directly from the Pull OTA task or the setup() task.
    assert_contains(PULL_OTA_HEADER, "remoteQueueResetAt(millis())")
    pull_ota_contents = PULL_OTA_HEADER.read_text(encoding="utf-8")
    if pull_ota_contents.count("remoteQueueResetAt(millis())") != 2:
        raise AssertionError(
            "pull_ota.h expected exactly two queued restarts "
            "(pullOtaInstall and pullOtaResumePendingLittleFs)"
        )
    assert_not_contains(PULL_OTA_HEADER, "ESP.restart();\n  return true;")

    # hdsOtaRollback() withdraws mDNS/WiFi before the rollback reboot, since
    # the pending-LittleFS flow can have brought WiFi (and mDNS) up first.
    assert_before(
        ROLLBACK_HEADER,
        "stopWifi();",
        "esp_ota_mark_app_invalid_rollback_and_reboot();",
    )

    # setHostname() must run before WiFi.config(), which starts STA and
    # copies the then-current default hostname to the interface.
    assert_before(
        WIFI_SETUP_SOURCE,
        "WiFi.setHostname(params.getMdnsName());",
        "WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);",
    )

    assert_ordered(
        WEBSOCKET_HEADER,
        [
            "uint32_t mask = b_ota ? (wsPendingMask & WSP_RESET) : wsPendingMask;",
            "wsPendingMask &= ~mask;",
            "if (mask & WSP_RESET)",
            "if (mask & WSP_DISPLAY_ON)",
        ],
    )
    assert_ordered(
        WEBSOCKET_HEADER,
        [
            "if (mask & WSP_WIFI_UPDATE)",
            "wifiUpdate();",
            "if (b_ota) return;",
            "if (mask & WSP_POWER_OFF)",
        ],
    )

    print("OTA reboot routing contract tests passed")


if __name__ == "__main__":
    main()
