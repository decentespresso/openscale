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
    require("include/custom_build_ota.h", 'customBuildShowRelink("Already installed", hashPrefix)')
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
    assert check_in_body.index("deserializeJson") < check_in_body.index("customBuildHexValueValid")
    assert "std::vector" not in header
    assert "static const uint32_t HDS_OTA_TASK_STACK_BYTES" not in header
    assert '"/api/v1/fleet/' not in header
    assert "fleet_secret" not in header
    require("include/menu.h", '"Custom Build", customBuildMenu')
    require("src/hds.ino", '#include "custom_build_ota.h"')
    require(".github/workflows/custom-build.yml", "HDS_CUSTOM_OTA_SIGNING_KEY_PEM")
    release = require(".github/workflows/release.yml", "python tools/write_custom_ota_public_key_header.py")
    assert "HDS_CUSTOM_OTA_SIGNING_KEY_PEM" not in release
    print("custom build OTA contract tests passed")


if __name__ == "__main__":
    main()
