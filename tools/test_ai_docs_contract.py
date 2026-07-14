import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def find_call(source, name, start=0):
    match = re.search(rf"\b{re.escape(name)}\s*\(", source[start:])
    if match is None:
        raise AssertionError(f"missing function call: {name}")
    return start + match.start()


def main():
    wifi_source = read("src/wifi_setup.cpp")
    access_point = re.search(
        r'WiFi\s*\.\s*softAP\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)',
        wifi_source,
    )
    if access_point is None:
        raise AssertionError("src/wifi_setup.cpp missing literal WiFi.softAP credentials")

    ssid, password = access_point.groups()
    readme = read("README.md")
    if f"SSID `{ssid}`" not in readme or f"password `{password}`" not in readme:
        raise AssertionError("README.md access-point credentials do not match setupAP()")

    ota_notes = read("docs/AI_OTA_NOTES.md")
    hds_source = read("src/hds.ino")
    pull_ota = read("include/pull_ota.h")
    rollback = read("include/ota_rollback.h")
    functions = {
        "hdsOtaRollbackBegin": rollback,
        "hdsOtaRollbackMarkValid": rollback,
        "pullOtaResumePendingLittleFs": pull_ota,
        "pullOtaLoadPendingLittleFs": pull_ota,
    }
    for name, source in functions.items():
        if f"`{name}()`" not in ota_notes or not re.search(rf"\b{re.escape(name)}\s*\(", source):
            raise AssertionError(f"AI_OTA_NOTES.md function reference is stale: {name}")

    for key in ("target_try", "fs_dirty", "restore", "restore_try"):
        if f"`{key}`" not in ota_notes or f'"{key}"' not in pull_ota:
            raise AssertionError(f"AI_OTA_NOTES.md NVS key is stale: {key}")

    pending_match = re.search(r"\bif\s*\(\s*b_pendingOtaLittleFs\s*\)", hds_source)
    if pending_match is None:
        raise AssertionError("src/hds.ino missing pending LittleFS recovery branch")
    pending_start = pending_match.start()
    pending_resume = find_call(hds_source, "pullOtaResumePendingLittleFs", pending_start)
    normal_mark_valid = find_call(hds_source, "hdsOtaRollbackMarkValid", pending_start)
    if pending_resume >= normal_mark_valid:
        raise AssertionError("pending LittleFS recovery must precede normal validity marking")

    resume_start = find_call(pull_ota, "pullOtaResumePendingLittleFs")
    resume_end = find_call(pull_ota, "pullOtaRunUpdate", resume_start)
    resume_body = pull_ota[resume_start:resume_end]
    if find_call(resume_body, "pullOtaClearPendingLittleFs") >= find_call(
        resume_body, "hdsOtaRollbackMarkValid"
    ):
        raise AssertionError("successful LittleFS recovery must clear pending state before validity marking")
    if not re.search(
        r"maxAttempts\s*=\s*pending\.restore\s*\?\s*1\s*:\s*2",
        resume_body,
    ):
        raise AssertionError("LittleFS recovery attempt limits changed")
    if not re.search(r"\btwo attempts\b", ota_notes) or not re.search(
        r"\bone (?:recorded )?restore attempt\b", ota_notes
    ):
        raise AssertionError("AI_OTA_NOTES.md attempt limits are stale")

    repo_map = read("docs/AI_REPO_MAP.md")
    references = set(re.findall(r"`([^`]+)`", repo_map))
    path_prefixes = (
        "include/",
        "src/",
        "docs/",
        "tools/",
        "web_apps/",
        ".github/",
        "Hardware/",
        "Scale Case/",
    )
    paths = {
        reference
        for reference in references
        if "*" not in reference
        and (reference.startswith(path_prefixes) or reference in {"platformio.ini", "README.md"})
    }
    missing = sorted(path for path in paths if not (ROOT / path).exists())
    if missing:
        raise AssertionError(f"AI_REPO_MAP.md references missing paths: {', '.join(missing)}")

    print("AI documentation contract tests passed")


if __name__ == "__main__":
    main()
