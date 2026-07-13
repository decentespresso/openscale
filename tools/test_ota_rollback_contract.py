from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"


def assert_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def main():
    assert_contains(HDS_SOURCE, '#include "ota_rollback.h"')
    assert_contains(HDS_SOURCE, "hdsOtaRollbackBegin(r);")
    assert_contains(HDS_SOURCE, "hdsOtaRollbackMarkValid();")
    assert_contains(HDS_SOURCE, "if (!pullOtaResumePendingLittleFs())")
    assert_contains(HDS_SOURCE, 'hdsOtaRollback("LittleFS update");')
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_PENDING_WIFI_TIMEOUT_MS = 10000")
    assert_contains(PULL_OTA_HEADER, "pullOtaEnsureWifi(HDS_OTA_PENDING_WIFI_TIMEOUT_MS)")
    assert_contains(PULL_OTA_HEADER, "hdsOtaRollbackMarkValid();")
    assert_contains(PULL_OTA_HEADER, "pullOtaFindCurrentRelease(catalog, rollbackManifest)")
    assert_contains(PULL_OTA_HEADER, "pullOtaBeginRollbackLittleFsAttempt()")
    assert_contains(PULL_OTA_HEADER, "maxAttempts = pending.restore ? 1 : 2")
    assert_contains(PULL_OTA_HEADER, "loaded.filesystemDirty")
    assert_contains(PULL_OTA_HEADER, "pending.restoreAttempted")
    assert_contains(PULL_OTA_HEADER, "pullOtaRecoveryError();")
    assert_contains(ROLLBACK_HEADER, 'extern "C" bool verifyRollbackLater()')
    assert_contains(ROLLBACK_HEADER, "return true;")
    assert_contains(ROLLBACK_HEADER, "ESP_OTA_IMG_PENDING_VERIFY")
    assert_contains(ROLLBACK_HEADER, "esp_ota_mark_app_valid_cancel_rollback")
    assert_contains(ROLLBACK_HEADER, "esp_ota_mark_app_invalid_rollback_and_reboot")
    assert_contains(ROLLBACK_HEADER, 'preferences.begin("ota_verify"')
    assert_contains(ROLLBACK_HEADER, 'preferences.putUInt("attempts"')
    assert_contains(ROLLBACK_HEADER, "LittleFS.begin()")
    assert_contains(ROLLBACK_HEADER, "LittleFS.totalBytes()")
    print("OTA rollback contract tests passed")


if __name__ == "__main__":
    main()
