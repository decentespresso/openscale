from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDS_SOURCE = ROOT / "src" / "hds.ino"
MENU_HEADER = ROOT / "include" / "menu.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"
OTA_STAGE_MARKER = ROOT / "plugins" / "default-web-apps" / "assets" / ("ota-stage-" + "test.txt")


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
    if contents.find(first) >= contents.find(second):
        raise AssertionError(f"{path.name} must place {first} before {second}")


def function_body(source, name):
    start = source.index(f"{name}(")
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
    assert_contains(HDS_SOURCE, '#include "pull_ota.h"')
    assert_contains(MENU_HEADER, "menuWiFiPullUpdateOption")
    assert_contains(MENU_HEADER, "&menuWiFiPullUpdateOption")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_URL")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_PUBLIC_KEY_1_PEM")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_PUBLIC_KEY_2_PEM")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_PUBLIC_KEY_3_PEM")
    assert_contains(PULL_OTA_HEADER, "pullOtaPublicKeysConfigured")
    assert_contains(PULL_OTA_HEADER, "for (const char *key : publicKeys)")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchSignedManifest")
    assert_contains(PULL_OTA_HEADER, "pullOtaManifestUrlAllowed")
    assert_contains(PULL_OTA_HEADER, "pullOtaVerifyManifestSignature")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchManifestSignature")
    assert_contains(PULL_OTA_HEADER, "mbedtls_pk_verify")
    assert_contains(PULL_OTA_HEADER, "sha256Matches")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseVersionTriplet")
    assert_contains(PULL_OTA_HEADER, "pullOtaNormalizeVersionPrefix")
    assert_contains(PULL_OTA_HEADER, "Update.begin(asset.size, command)")
    assert_contains(PULL_OTA_HEADER, "pullOtaManifestCompatible")
    assert_contains(PULL_OTA_HEADER, "pullOtaSelectRelease")
    assert_contains(PULL_OTA_HEADER, "PullOtaReleaseList")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MANIFEST_MAX_BYTES = 16384")
    assert_contains(PULL_OTA_HEADER, "HDS_OTA_MAX_RELEASE_CHOICES = 10")
    assert_contains(PULL_OTA_HEADER, "pullOtaReleaseCanRunFromCurrent")
    assert_contains(PULL_OTA_HEADER, 'HDS_OTA_MIN_INSTALL_VERSION = "3.1.13"')
    assert_contains(PULL_OTA_HEADER, "pullOtaCompareVersions(manifest.version, HDS_OTA_MIN_INSTALL_VERSION) < 0")
    assert_contains(PULL_OTA_HEADER, "pullOtaBuildSelectableReleases")
    assert_contains(PULL_OTA_HEADER, "PullOtaReleaseSelection")
    assert_contains(PULL_OTA_HEADER, "currentCompare != 0")
    assert_contains(PULL_OTA_HEADER, "pullOtaHasNewerRelease")
    assert_contains(PULL_OTA_HEADER, '"Newest stable"')
    assert_contains(PULL_OTA_HEADER, '"Install version"')
    assert_contains(PULL_OTA_HEADER, "pullOtaPickRelease")
    assert_contains(PULL_OTA_HEADER, "pullOtaDrawReleaseChoice")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseManifest(body, catalog)")
    assert_contains(PULL_OTA_HEADER, "pullOtaBuildSelectableReleases(catalog, selection)")
    assert_contains(PULL_OTA_HEADER, "pullOtaPickRelease(catalog, selection, &selectedCatalogIndex)")
    assert_contains(PULL_OTA_HEADER, "std::move(catalog.releases[selectedCatalogIndex])")
    assert_not_contains(PULL_OTA_HEADER, "PullOtaReleaseList releases;")
    assert_contains(PULL_OTA_HEADER, "pullOtaFindCurrentRelease(catalog, rollbackManifest)")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchCurrentReleaseManifest(rollbackManifest)")
    assert_contains(PULL_OTA_HEADER, "pullOtaReleaseManifestUrl(currentVersion, prefixedTag)")
    assert_contains(PULL_OTA_HEADER, "const bool prefixedTags[] = {true, false}")
    assert_contains(PULL_OTA_HEADER, "pullOtaFetchSignedManifest(")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseRollbackManifest(body, currentVersion, manifest)")
    assert_contains(PULL_OTA_HEADER, "pullOtaParseManifestObject(root, candidate)")
    assert_contains(PULL_OTA_HEADER, "pullOtaCompareVersions(candidate.version, currentVersion) != 0")
    assert_contains(PULL_OTA_HEADER, "!candidate.littlefs.present || !candidate.littlefs.required")
    assert_before(
        PULL_OTA_HEADER,
        "if (selection.count == 0)",
        "pullOtaFindCurrentRelease(catalog, rollbackManifest)",
    )
    assert_before(
        PULL_OTA_HEADER,
        "rollbackFound = pullOtaFindCurrentRelease(catalog, rollbackManifest)",
        "!rollbackFound && !pullOtaFetchCurrentReleaseManifest(rollbackManifest)",
    )
    assert_contains(PULL_OTA_HEADER, "wifi_init();")
    assert_contains(PULL_OTA_HEADER, "pullOtaStorePendingLittleFs")
    assert_contains(PULL_OTA_HEADER, "pullOtaLoadPendingLittleFs")
    assert_contains(PULL_OTA_HEADER, "loaded.version != pullOtaCurrentVersion()")
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
    pull_ota = PULL_OTA_HEADER.read_text(encoding="utf-8")
    assert "b_ota = false;" not in function_body(pull_ota, "pullOtaFail")
    assert "b_ota = false;" not in function_body(pull_ota, "pullOtaInstall")
    update_task = function_body(pull_ota, "pullOtaUpdateTask")
    assert update_task.index("otaRuntimeIsPaused()") < update_task.index("pullOtaRunUpdate")
    assert "millis() - pauseStartedAt < OTA_RUNTIME_PAUSE_TIMEOUT_MS" in update_task
    assert "pullOtaFail(\"OTA runtime pause failed\")" in update_task
    assert "restartPending = (wsPendingMask & WSP_OTA_RESET) != 0" in update_task
    assert "if (!restartPending)" in update_task
    update = function_body(pull_ota, "pullOtaUpdate")
    assert "b_pullOtaRunning || b_ota" in update
    if OTA_STAGE_MARKER.exists():
        raise AssertionError("OTA stage marker must not be tracked")
    assert_contains(PULL_OTA_HEADER, "void pullOtaRunUpdate(const PullOtaTargetVersion &target) {")
    assert_contains(PULL_OTA_HEADER, "void pullOtaUpdate(const PullOtaTargetVersion &target) {")
    assert_contains(PULL_OTA_HEADER, "pullOtaRunUpdate(pullOtaLoadRequestedTarget())")
    assert_contains(PULL_OTA_HEADER, "pullOtaFindTargetRelease")
    assert_contains(PULL_OTA_HEADER, "for (uint8_t i = 0; i < selection.count; i++) {")
    assert_contains(PULL_OTA_HEADER, "pullOtaFindTargetRelease(catalog, selection, target, &selectedCatalogIndex)")
    assert_contains(PULL_OTA_HEADER, 'pullOtaFail("Version not offered")')

    contents = PULL_OTA_HEADER.read_text(encoding="utf-8")
    run_start = contents.index("void pullOtaRunUpdate(const PullOtaTargetVersion &target) {")
    run = contents[run_start:contents.index("void pullOtaStoreRequestedTarget(", run_start)]

    resume_at = run.index("pullOtaResumePendingLittleFs();")
    if resume_at > run.index("if (target.present) {"):
        raise AssertionError("a pending LittleFS transaction must take priority over a requested target")

    targeted_start = run.index("if (target.present) {", run.index("uint8_t selectedCatalogIndex = 0;"))
    targeted = run[targeted_start:run.index("if (!pullOtaHasNewerRelease(catalog, selection)) {")]
    if "pullOtaPickRelease" in targeted or "pullOtaConfirmInstall" in targeted:
        raise AssertionError("an unattended install must not open the picker or the confirm prompt")
    if "pullOtaInstall(manifest, rollbackManifest);" not in targeted:
        raise AssertionError("an unattended install must reuse pullOtaInstall")
    if run.index("pullOtaFindCurrentRelease(catalog, rollbackManifest)") > targeted_start:
        raise AssertionError("the rollback manifest must be resolved before an unattended install")

    interactive = run[run.index("if (!pullOtaHasNewerRelease(catalog, selection)) {"):]
    if "pullOtaPickRelease(catalog, selection, &selectedCatalogIndex)" not in interactive:
        raise AssertionError("the interactive picker must remain on the no-target path")
    if "pullOtaConfirmInstall(manifest)" not in interactive:
        raise AssertionError("the confirm prompt must remain on the no-target path")

    parameter = PARAMETER_HEADER.read_text(encoding="utf-8")
    for name in ("pendingOtaTargetMajor", "pendingOtaTargetMinor", "pendingOtaTargetPatch",
                 "requestedOtaTargetMajor", "requestedOtaTargetMinor", "requestedOtaTargetPatch"):
        if f"volatile uint8_t {name}" not in parameter:
            raise AssertionError(f"{name} must be volatile in parameter.h")
    for name in ("pendingOtaTargetPresent", "requestedOtaTargetPresent"):
        if f"volatile bool {name}" not in parameter:
            raise AssertionError(f"{name} must be volatile in parameter.h")

    for helper in ("void pullOtaStoreRequestedTarget(", "PullOtaTargetVersion pullOtaLoadRequestedTarget("):
        start = contents.index(helper)
        body = contents[start:contents.index("}", contents.index("portEXIT_CRITICAL", start))]
        if "portENTER_CRITICAL(&wsPendingMux);" not in body:
            raise AssertionError(f"{helper} must access the requested target under wsPendingMux")

    if "strcmp(offered, wanted) == 0" not in contents:
        raise AssertionError("target matching must compare normalized versions exactly")
    if "pullOtaCompareVersions(catalog.releases[catalogIndex].version" in contents:
        raise AssertionError(
            "target matching must not use pullOtaCompareVersions, which returns 0 for unparseable input")

    websocket = WEBSOCKET_HEADER.read_text(encoding="utf-8")
    helper_start = websocket.index("inline bool remoteQueueWifiUpdate(")
    helper = websocket[helper_start:websocket.index("inline void wsQueuePending(", helper_start)]
    if "portENTER_CRITICAL(&wsPendingMux);" not in helper or "portEXIT_CRITICAL(&wsPendingMux);" not in helper:
        raise AssertionError("the pending target must be written under wsPendingMux")

    print("pull OTA contract tests passed")


if __name__ == "__main__":
    main()
