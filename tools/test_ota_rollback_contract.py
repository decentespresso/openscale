from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"


def assert_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def assert_before(path, left, right):
    contents = path.read_text(encoding="utf-8")
    left_index = contents.find(left)
    right_index = contents.find(right)
    if left_index < 0 or right_index < 0 or left_index >= right_index:
        raise AssertionError(f"{path.name} expected {left} before {right}")


def main():
    assert_contains(HDS_SOURCE, '#include "ota_rollback.h"')
    assert_contains(HDS_SOURCE, "hdsOtaRollbackBegin(r);")
    assert_contains(HDS_SOURCE, "hdsOtaRollbackMarkValid();")
    assert_contains(HDS_SOURCE, "pullOtaResumePendingLittleFs();")
    assert_before(HDS_SOURCE, "hdsOtaRollbackMarkValid();", "pullOtaResumePendingLittleFs();")
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
