from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_OTA_HEADER = ROOT / "include" / "wifi_ota.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"
WIFI_SETUP_SOURCE = ROOT / "src" / "wifi_setup.cpp"
HDS_SOURCE = ROOT / "src" / "hds.ino"


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


def main():
    hds = HDS_SOURCE.read_text(encoding="utf-8")
    loop = hds[hds.index("void loop() {"):hds.index("void chargingOLED(")]
    if "if (b_ota || bleHasLiveClient()" not in loop:
        raise AssertionError("OTA does not suspend the auto-sleep countdown")

    # ElegantOTA's own auto-reboot bypasses reset()'s mDNS withdrawal; the
    # restart must be queued through the main-loop reset mechanism instead.
    assert_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(false)")
    assert_not_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(true)")
    assert_contains(WIFI_OTA_HEADER, "remoteQueueResetAt(millis() + OTA_RESTART_DELAY_MS)")

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

    print("OTA reboot routing contract tests passed")


if __name__ == "__main__":
    main()
