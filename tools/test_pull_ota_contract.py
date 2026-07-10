from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
MENU_HEADER = ROOT / "include" / "menu.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
OTA_STAGE_MARKER = ROOT / "web_apps" / ("ota-stage-" + "test.txt")


def assert_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def assert_not_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text in contents:
        raise AssertionError(f"{path.name} contains {text}")


def main():
    assert_contains(HDS_SOURCE, '#include "pull_ota.h"')
    assert_contains(MENU_HEADER, "menuWiFiPullUpdateOption")
    assert_contains(MENU_HEADER, "&menuWiFiPullUpdateOption")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_URL")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_PUBLIC_KEY_PEM")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchSignedManifest")
    assert_contains(PULL_OTA_HEADER, "pullOtaVerifyManifestSignature")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchManifestSignature")
    assert_contains(PULL_OTA_HEADER, "mbedtls_pk_verify")
    assert_contains(PULL_OTA_HEADER, "sha256Matches")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseVersionTriplet")
    assert_contains(PULL_OTA_HEADER, "Update.begin(asset.size, command)")
    assert_contains(PULL_OTA_HEADER, "pullOtaManifestCompatible")
    assert_contains(PULL_OTA_HEADER, "pullOtaSelectRelease")
    assert_contains(PULL_OTA_HEADER, "PullOtaReleaseList")
    assert_contains(PULL_OTA_HEADER, "pullOtaReleaseCanRunFromCurrent")
    assert_contains(PULL_OTA_HEADER, 'HDS_OTA_MIN_INSTALL_VERSION = "3.1.13"')
    assert_contains(PULL_OTA_HEADER, "pullOtaCompareVersions(manifest.version, HDS_OTA_MIN_INSTALL_VERSION) < 0")
    assert_contains(PULL_OTA_HEADER, "pullOtaBuildSelectableReleases")
    assert_contains(PULL_OTA_HEADER, "currentCompare != 0")
    assert_contains(PULL_OTA_HEADER, "pullOtaHasNewerRelease")
    assert_contains(PULL_OTA_HEADER, '"Newest stable"')
    assert_contains(PULL_OTA_HEADER, '"Install version"')
    assert_contains(PULL_OTA_HEADER, "pullOtaPickRelease")
    assert_contains(PULL_OTA_HEADER, "pullOtaDrawReleaseChoice")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseManifest(body, catalog)")
    assert_contains(PULL_OTA_HEADER, "pullOtaBuildSelectableReleases(catalog, releases)")
    assert_contains(PULL_OTA_HEADER, "pullOtaFindCurrentRelease(catalog, rollbackManifest)")
    assert_contains(PULL_OTA_HEADER, "wifi_init();")
    assert_contains(PULL_OTA_HEADER, "pullOtaStorePendingLittleFs")
    assert_contains(PULL_OTA_HEADER, "pullOtaLoadPendingLittleFs")
    assert_contains(PULL_OTA_HEADER, "pullOtaResumePendingLittleFs")
    assert_contains(PULL_OTA_HEADER, "pullOtaClearPendingLittleFs")
    assert_contains(PULL_OTA_HEADER, 'preferences.begin("ota_fs"')
    assert_contains(PULL_OTA_HEADER, "pullOtaStreamAsset(manifest.firmware, U_FLASH")
    assert_contains(PULL_OTA_HEADER, "pending.asset, U_SPIFFS, \"LittleFS\", &filesystemWriteStarted")
    assert_contains(PULL_OTA_HEADER, 'preferences.putString("rb_url"')
    assert_contains(PULL_OTA_HEADER, 'preferences.putString("rb_sha"')
    assert_contains(PULL_OTA_HEADER, 'preferences.putBool("restore_try", true)')
    assert_contains(PULL_OTA_HEADER, 'preferences.putBool("fs_dirty", true)')
    assert_contains(PULL_OTA_HEADER, "maxAttempts = pending.restore ? 1 : 2")
    assert_contains(PULL_OTA_HEADER, "pullOtaBeginTargetLittleFsAttempt(attempts)")
    assert_contains(PULL_OTA_HEADER, "pullOtaActivateRollbackLittleFs(pending)")
    assert_contains(PULL_OTA_HEADER, 'pullOtaDraw("UPDATE ERROR", "Use HDS updater!")')
    assert_contains(PULL_OTA_HEADER, "pullOtaPartitionShaMatches")
    assert_contains(PULL_OTA_HEADER, "ESP.getFlashChipSize()")
    assert_contains(PULL_OTA_HEADER, "ESP.getFlashChipSize() < manifest.flashSize")
    assert_contains(PULL_OTA_HEADER, "ESP.getFreeSketchSpace()")
    assert_contains(PULL_OTA_HEADER, "LittleFS.totalBytes()")
    assert_contains(PULL_OTA_HEADER, 'pullOtaFail("Signature failed")')
    assert_not_contains(PULL_OTA_HEADER, "HDS_OTA_ALLOW_TEST_" + "DOWNGRADES")
    assert_not_contains(PULL_OTA_HEADER, "HDS_OTA_TEST_MIN_" + "VERSION")
    assert_not_contains(PULL_OTA_HEADER, "pullOtaSet" + "Status")
    assert_not_contains(PULL_OTA_HEADER, "pullOtaLoadBoot" + "Status")
    assert_not_contains(PULL_OTA_HEADER, "ota_" + "status")
    assert_not_contains(PULL_OTA_HEADER, "ODev" + "Studio")
    assert_not_contains(HDS_SOURCE, "drawPullOta" + "Status")
    assert_not_contains(MENU_HEADER, "ODev" + "Studio")
    assert_not_contains(PULL_OTA_HEADER, "FS update needed")
    assert_not_contains(PULL_OTA_HEADER, "setupWifi();")
    assert_not_contains(PULL_OTA_HEADER, "setInsecure")
    assert_not_contains(PULL_OTA_HEADER, "if (list.count == 1)")
    if OTA_STAGE_MARKER.exists():
        raise AssertionError("OTA stage marker must not be tracked")
    print("pull OTA contract tests passed")


if __name__ == "__main__":
    main()
