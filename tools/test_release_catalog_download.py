import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap

from generate_release_manifest import build_manifest


ROOT = Path(__file__).resolve().parents[1]
BASH = "C:/Program Files/Git/bin/bash.exe" if os.name == "nt" else "bash"
MOCK = r'''
gh() {
  if [ "$2" = list ]; then
    [ "$SCENARIO" != list_failure ] || return 1
    [ "$SCENARIO" != empty ] || return 0
    echo "$STABLE_TAG"
    return 0
  fi
  [ "$3" = "$STABLE_TAG" ] || return 1
  local asset="$7"
  echo "$asset" >> downloads
  case "$SCENARIO" in
    both_fail) return 1 ;;
    missing_json) [ "$asset" != manifest.json ] || return 0 ;;
    missing_sig) [ "$asset" != manifest.sig ] || return 0 ;;
    retry) [ "$attempt" -gt 1 ] || return 1 ;;
    partial_retry) [ "$attempt" -gt 1 ] || [ "$asset" != manifest.sig ] || return 1 ;;
  esac
  cp "fixtures/$asset" "previous-release/$asset"
}
sleep() { :; }
python() { "$TEST_PYTHON" "$@"; }
'''


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True)


def main():
    workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
    verifier = textwrap.dedent(workflow.split("          verifyManifestSignature() {", 1)[1].split('          if [ -z', 1)[0])
    verifier = "verifyManifestSignature() {\n" + verifier
    catalog = textwrap.dedent(workflow.split("          PREVIOUS_MANIFEST_ARGS=()", 1)[1].split('          if [ ! -f release-files/manifest.sig', 1)[0])
    script = "set -euo pipefail\n" + MOCK + verifier + "PREVIOUS_MANIFEST_ARGS=()\n" + catalog
    scenarios = (
        ("both_fail", False, False, 3),
        ("missing_json", False, False, 6),
        ("missing_sig", False, False, 6),
        ("invalid_signature", False, False, 2),
        ("retry", False, True, 3),
        ("partial_retry", False, True, 4),
        ("success", False, True, 2),
        ("success", True, True, 2),
        ("empty", False, False, 0),
        ("empty", True, True, 0),
        ("both_fail", True, False, 3),
        ("invalid_signature", True, False, 2),
        ("list_failure", True, False, 0),
    )
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", "private.pem"], base)
        run(["openssl", "pkey", "-in", "private.pem", "-pubout", "-out", "public.pem"], base)
        for index, (scenario, bootstrap, success, downloads) in enumerate(scenarios):
            case = base / str(index)
            for folder in ("keys/ota", "fixtures", "include", "tools", ".pio.nosync/build/esp32s3"):
                (case / folder).mkdir(parents=True, exist_ok=True)
            for key in range(1, 4):
                shutil.copyfile(base / "public.pem", case / f"keys/ota/hds_ota_manifest_public_key_{key}.pem")
            shutil.copyfile(ROOT / "tools/generate_release_manifest.py", case / "tools/generate_release_manifest.py")
            (case / "include/config.h").write_text("", encoding="utf-8")
            for asset in ("firmware.bin", "littlefs.bin"):
                (case / ".pio.nosync/build/esp32s3" / asset).write_bytes(asset.encode())
            previous = build_manifest(
                case / ".pio.nosync/build/esp32s3", "v3.1.13",
                "decentespresso/openscale", "hds", "3.0.0",
            )
            (case / "fixtures/manifest.json").write_text(json.dumps(previous), encoding="utf-8")
            run(["openssl", "dgst", "-sha256", "-sign", str(base / "private.pem"), "-out", "fixtures/manifest.sig", "fixtures/manifest.json"], case)
            if scenario == "invalid_signature":
                (case / "fixtures/manifest.json").write_text('{"version":"3.1.12"}', encoding="utf-8")
            result = subprocess.run(
                [BASH, "-c", script], cwd=case, capture_output=True, text=True,
                env={**os.environ, "SCENARIO": scenario, "STABLE_TAG": "v3.1.13",
                     "BOOTSTRAP_CATALOG": str(bootstrap).lower(), "TAG": "v3.1.14",
                     "GITHUB_REPOSITORY": "decentespresso/openscale",
                     "TEST_PYTHON": Path(sys.executable).as_posix(),
                     "HDS_OTA_SIGNING_KEY_FILE": str(base / "private.pem")},
            )
            assert (result.returncode == 0) == success, (scenario, result.stdout, result.stderr)
            log = case / "downloads"
            assert (len(log.read_text().splitlines()) if log.exists() else 0) == downloads, scenario
            output = case / "release-files/manifest.json"
            assert output.exists() == success, scenario
            if success:
                entries = json.loads(output.read_text())["releases"]
                versions = [entry["version"] for entry in entries]
                assert versions == (["3.1.14"] if scenario == "empty" else ["3.1.14", "3.1.13"]), versions
                if scenario != "empty":
                    assert entries[1] == previous
    print("release catalog download runtime tests passed")


if __name__ == "__main__":
    main()
