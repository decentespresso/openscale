from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_OTA_HEADER = ROOT / "include" / "wifi_ota.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"
WIFI_SETUP_SOURCE = ROOT / "src" / "wifi_setup.cpp"


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
    assert_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(false)")
    assert_not_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(true)")
    assert_contains(WIFI_OTA_HEADER, "remoteQueueResetAt(millis() + OTA_RESTART_DELAY_MS)")

    assert_contains(PULL_OTA_HEADER, "remoteQueueResetAt(millis())")
    pull_ota_contents = PULL_OTA_HEADER.read_text(encoding="utf-8")
    if pull_ota_contents.count("remoteQueueResetAt(millis())") != 2:
        raise AssertionError(
            "pull_ota.h expected exactly two queued restarts "
            "(pullOtaInstall and pullOtaResumePendingLittleFs)"
        )
    assert_not_contains(PULL_OTA_HEADER, "ESP.restart();\n  return true;")

    assert_before(
        ROLLBACK_HEADER,
        "stopWifi();",
        "esp_ota_mark_app_invalid_rollback_and_reboot();",
    )

    assert_before(
        WIFI_SETUP_SOURCE,
        "WiFi.setHostname(params.getMdnsName());",
        "WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);",
    )

    print("OTA reboot routing contract tests passed")


if __name__ == "__main__":
    main()
