from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(path, value):
    text = (ROOT / path).read_text(encoding="utf-8")
    assert value in text, f"{path} missing {value}"
    return text


def function_body(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


def main():
    header = require("include/custom_build_ota.h", 'HDS_CUSTOM_BUILD_NVS_NAMESPACE[] = "ota_custom"')
    require("include/custom_build_ota.h", "customBuildRandomHex(newDeviceId, 16)")
    require("include/custom_build_ota.h", "customBuildRandomHex(newDeviceSecret, 32)")
    require("include/custom_build_ota.h", "pullOtaVerifyManifestSignatureWithKeys")
    require("include/custom_build_ota.h", "HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM")
    require("include/custom_build_ota.h", "HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM")
    require("include/custom_build_ota.h", 'pullOtaDraw("Pair code", pairCode, "Valid 12 hours")')
    require("include/custom_build_ota.h", 'String(HDS_CUSTOM_BUILD_SERVICE_URL) + "/v1/" + combinationHash')
    require("include/custom_build_ota.h", "customBuildFetchManifest(rollbackCombinationHash, rollbackManifest)")
    require("include/custom_build_ota.h", 'customBuildRequest("/api/v1/device/check-in", "POST"')
    require("include/custom_build_ota.h", 'request["installed_combination"]')
    require("include/custom_build_ota.h", 'request["firmware_version"]')
    require("include/custom_build_ota.h", 'customBuildShowStatus("Already installed", hashPrefix)')
    hold = function_body(header, "bool customBuildWaitForHold(")
    cancel = function_body(hold, "if (cancelPin >= 0")
    assert "digitalRead(cancelPin) == LOW" in hold
    assert "pullOtaWaitForRelease(1000);" in cancel
    assert "return false;" in cancel
    assert hold.index("cancelPin >= 0") < hold.index("digitalRead(pin)")
    status = function_body(header, "void customBuildShowStatus(")
    wait = "customBuildWaitForHold(BUTTON_CIRCLE, HDS_CUSTOM_BUILD_SCREEN_TIMEOUT_MS, BUTTON_SQUARE)"
    assert 'pullOtaDraw(line1, line2, "Sq back")' in status
    assert "customBuildWaitForDismiss(HDS_CUSTOM_BUILD_SCREEN_TIMEOUT_MS)" in status
    assert "customBuildPairScale" not in status
    assert "Relink" not in status
    confirmation = function_body(header, "bool customBuildConfirmRelink()")
    assert "if (!pullOtaWaitForRelease(3000)) return false;" in confirmation
    assert confirmation.index("pullOtaWaitForRelease(3000)") < confirmation.index('pullOtaDraw("Relink scale?"')
    assert confirmation.index('pullOtaDraw("Relink scale?"') < confirmation.index(wait)
    assert "if (customBuildConfirmRelink()) customBuildPairScale(false);" in header
    availability = function_body(header, "bool customBuildRelinkAvailable()")
    assert "preferences.begin(HDS_CUSTOM_BUILD_NVS_NAMESPACE, true)" in availability
    assert 'preferences.getBool("pair_init", false)' in availability
    assert "putBool" not in availability
    assert "customBuildStart(true);" in function_body(header, "void customBuildRelinkMenu()")
    assert "customBuildStart(false);" in function_body(header, "void customBuildMenu()")
    install_prompt = function_body(header, "bool customBuildConfirmInstall(")
    assert "relink" not in install_prompt.lower()
    require("include/custom_build_ota.h", 'pullOtaDraw(manifest.version.c_str(), hashPrefix')
    require("include/custom_build_ota.h", "assignment.combinationHash,")
    require("include/custom_build_ota.h", "rollbackCombinationHash);")
    pull_ota = require("include/pull_ota.h", 'preferences.putString("combo", combinationHash) == 64')
    assert 'preferences.putString("rb_combo", rollbackCombinationHash) == 64' in pull_ota
    assert "pullOtaIdentityMatches(loaded.version, loaded.combinationHash)" in pull_ota
    assert "loaded.rollbackVersion, loaded.rollbackCombinationHash" in pull_ota
    assert "loaded.asset.url, loaded.combinationHash" in pull_ota
    assert "loaded.rollbackAsset.url, loaded.rollbackCombinationHash" in pull_ota
    assert '#define HDS_CUSTOM_BUILD_COMBINATION_HASH ""' in pull_ota
    assert "HDS_OTA_TASK_STACK_BYTES = 24576" in pull_ota
    current_hash = function_body(pull_ota, "String pullOtaCurrentCombinationHash()")
    assert "HDS_CUSTOM_BUILD_COMBINATION_HASH" in current_hash
    run = function_body(header, "void customBuildRun()")
    credentials = function_body(header, "bool customBuildLoadCredentials(")
    assert 'preferences.getBool("pair_init", false)' in credentials
    assert credentials.index('putBool("pair_init", false) == 1') < credentials.index('putString("device_id"')
    fresh = function_body(run, "if (!pairInitialized)")
    assert "customBuildPairScale(true);" in fresh
    assert "return;" in fresh
    assert run.index("if (!pairInitialized)") < run.index("pullOtaEnsureWifi()")
    assert run.index("if (!pairInitialized)") < run.index("customBuildCheckIn(")
    registration = function_body(header, "bool customBuildRegisterPairCode(")
    assert registration.index("customBuildRequest(") < registration.index("deserializeJson(")
    assert registration.index('!= pairCode) return false;') < registration.index('putBool("pair_init", true) == 1')
    assert 'return stored;' in registration
    pairing = function_body(header, "bool customBuildPairScale(")
    assert pairing.index("customBuildRegisterPairCode(pairCode)") < pairing.index('pullOtaDraw("Pair code"')
    assert 'pullOtaDraw("Custom Build", "Pair scale?", "Hold square")' in pairing
    rejected = function_body(run, "if (assignment.identityRejected)")
    assert 'customBuildShowStatus("Custom Build", "Device rejected");' in rejected
    assert "return;" in rejected
    assert run.index("if (assignment.identityRejected)") < run.index("if (!assignment.linked)")
    unlinked = function_body(run, "if (!assignment.linked)")
    assert 'customBuildShowStatus("Custom Build", "Not linked");' in unlinked
    assert "return;" in unlinked
    failure = function_body(run, "if (!customBuildCheckIn(")
    assert 'pullOtaFail("Service failed");' in failure
    installed = run.index("const String installedCombination = pullOtaCurrentCombinationHash()")
    check_in = run.index("customBuildCheckIn(installedCombination, assignment)")
    no_op = run.index("if (assignment.combinationHash == installedCombination)")
    ready_state = run.index('if (assignment.state == "queued"')
    manifest = run.index("customBuildFetchManifest(assignment.combinationHash, manifest)")
    install = run.index("pullOtaInstall(")
    assert installed < check_in < no_op < ready_state < manifest < install
    no_op_body = run[no_op:ready_state]
    assert "return;" in no_op_body
    for forbidden in (
        "customBuildFetchManifest",
        "customBuildFetchManifestSignature",
        "pullOtaStreamAsset",
        "pullOtaStorePendingLittleFs",
        "pullOtaInstall",
        "remoteQueueResetAt",
    ):
        assert forbidden not in no_op_body
    check_in_body = function_body(header, "bool customBuildCheckIn(")
    assert "body, &status)" in check_in_body
    assert "assignment.identityRejected = status == HTTP_CODE_UNAUTHORIZED;" in check_in_body
    assert "return assignment.identityRejected;" in check_in_body
    assert 'assignment.linked = root["linked"] | false;' in check_in_body
    assert check_in_body.rstrip().endswith("return true;")
    request = function_body(header, "bool customBuildRequest(")
    assert request.index("*responseStatus = 0") < request.index("customBuildLoadCredentials(")
    assert request.index("*responseStatus = status") < request.index("if (status != HTTP_CODE_OK)")
    assert "return false;" in function_body(request, "if (status != HTTP_CODE_OK)")
    for line in header.splitlines():
        if "pullOtaDraw(" in line or "printf" in line:
            assert "deviceSecret" not in line and "device_secret" not in line
    assert check_in_body.index("deserializeJson") < check_in_body.index("customBuildHexValueValid")
    assert "std::vector" not in header
    assert "static const uint32_t HDS_OTA_TASK_STACK_BYTES" not in header
    assert '"/api/v1/fleet/' not in header
    assert "fleet_secret" not in header
    menu = require("include/menu.h", '"Custom Build", customBuildMenu')
    assert '"Relink", customBuildRelinkMenu' in menu
    assert "getMenuSize(connectionsMenu) - (customBuildRelinkAvailable() ? 0 : 1)" in menu
    assert menu.count("currentMenuSize = connectionsMenuSize();") == 2
    require("src/hds.ino", '#include "custom_build_ota.h"')
    require(".github/workflows/custom-build.yml", "HDS_CUSTOM_OTA_SIGNING_KEY_PEM")
    release = require(".github/workflows/release.yml", "python tools/write_custom_ota_public_key_header.py")
    assert "HDS_CUSTOM_OTA_SIGNING_KEY_PEM" not in release
    print("custom build OTA contract tests passed")


if __name__ == "__main__":
    main()
