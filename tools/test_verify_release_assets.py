import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import zipfile

from generate_release_manifest import build_manifest, sign_manifest, write_manifest


ROOT = Path(__file__).resolve().parents[1]
BASH = "C:/Program Files/Git/bin/bash.exe" if os.name == "nt" else "bash"
MOCK = r'''
gh() {
  if [ "$2" = download ]; then
    [ "$SCENARIO" != download_failure ] || return 1
    mkdir downloaded-release
    cp uploaded/* downloaded-release/
  elif [ "$2" = edit ]; then
    touch published
  else
    return 1
  fi
}
python() { "$TEST_PYTHON" "$@"; }
'''


def main():
    workflow = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")
    publication = workflow.split('          gh release download "$TAG" --repo "$GITHUB_REPOSITORY" --dir downloaded-release', 1)[1]
    publication = publication.split("\n      - name:", 1)[0]
    script = "set -euo pipefail\n" + MOCK + 'gh release download "$TAG" --repo "$GITHUB_REPOSITORY" --dir downloaded-release\n' + textwrap.dedent(publication)
    names = ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin")
    scenarios = (
        "success", "preview", "download_failure", "signature", "manifest", "dependencies",
        "wrong_version", "wrong_size", "wrong_hash", "wrong_build", "swapped_manifest",
        "firmware.bin", "littlefs.bin", "missing_firmware", "missing_signature",
        "missing_zip", "zip_firmware.bin", "zip_bootloader.bin", "zip_partitions.bin",
        "zip_littlefs.bin", "zip_missing", "zip_extra", "zip_invalid",
        "wrong_embedded_version",
    )
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        key = base / "private.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(key)], check=True, capture_output=True)
        public = base / "public.pem"
        subprocess.run(["openssl", "pkey", "-in", str(key), "-pubout", "-out", str(public)], check=True, capture_output=True)
        for scenario in scenarios:
            case = base / scenario
            build = case / ".pio.nosync/build/esp32s3"
            expected = case / "release-files"
            uploaded = case / "uploaded"
            for folder in (build, expected, uploaded, case / "keys/ota", case / "tools"):
                folder.mkdir(parents=True, exist_ok=True)
            for index in range(1, 4):
                shutil.copyfile(public, case / f"keys/ota/hds_ota_manifest_public_key_{index}.pem")
            shutil.copyfile(ROOT / "tools/verify_release_assets.py", case / "tools/verify_release_assets.py")
            shutil.copyfile(ROOT / "tools/generate_release_manifest.py", case / "tools/generate_release_manifest.py")
            for name in names:
                (build / name).write_bytes(name.encode())
            tag = "v3.1.14-preview.4" if scenario == "preview" else "v3.1.14"
            version = tag.removeprefix("v") + ("-dev.abc" if scenario == "wrong_embedded_version" else "")
            (build / "firmware.bin").write_bytes(b"FW: " + version.encode() + b"\0")
            manifest = build_manifest(build, tag, "decentespresso/openscale", "hds", "3.0.0")
            if scenario == "wrong_version":
                manifest = {**manifest, "version": "3.1.13"}
            if scenario in ("wrong_size", "wrong_hash"):
                field, value = ("size", 999) if scenario == "wrong_size" else ("sha256", "0" * 64)
                manifest = {**manifest, "firmware": {**manifest["firmware"], field: value}}
            write_manifest(manifest, expected / "manifest.json")
            sign_manifest(expected / "manifest.json", expected / "manifest.sig", key)
            (expected / "dependencies.txt").write_bytes(b"dependency inventory")
            for name in ("manifest.json", "manifest.sig", "dependencies.txt"):
                shutil.copyfile(expected / name, uploaded / name)
            if scenario == "swapped_manifest":
                write_manifest({**manifest, "releases": []}, uploaded / "manifest.json")
                sign_manifest(uploaded / "manifest.json", uploaded / "manifest.sig", key)
            for name in ("firmware.bin", "littlefs.bin"):
                shutil.copyfile(build / name, uploaded / name)
            archivePath = uploaded / f"HDS_FW_{tag}.zip"
            with zipfile.ZipFile(archivePath, "w") as archive:
                for name in names:
                    if scenario == "zip_missing" and name == "bootloader.bin":
                        continue
                    data = (build / name).read_bytes()
                    archive.writestr(name, b"X" * len(data) if scenario == f"zip_{name}" else data)
                if scenario == "zip_extra":
                    archive.writestr("unexpected.bin", b"extra")
            corruptions = {"signature": "manifest.sig", "manifest": "manifest.json", "dependencies": "dependencies.txt",
                           "firmware.bin": "firmware.bin", "littlefs.bin": "littlefs.bin", "zip_invalid": archivePath.name}
            if scenario in corruptions:
                path = uploaded / corruptions[scenario]
                path.write_bytes(b"X" * path.stat().st_size)
            missing = {"missing_firmware": "firmware.bin", "missing_signature": "manifest.sig", "missing_zip": archivePath.name}
            if scenario in missing:
                (uploaded / missing[scenario]).unlink()
            if scenario == "wrong_build":
                (build / "firmware.bin").write_bytes(b"different build")
            result = subprocess.run(
                [BASH, "-c", script], cwd=case, capture_output=True, text=True,
                env={**os.environ, "SCENARIO": scenario, "TAG": tag,
                     "GITHUB_REPOSITORY": "decentespresso/openscale",
                     "TEST_PYTHON": Path(sys.executable).as_posix()},
            )
            success = scenario in ("success", "preview")
            assert (result.returncode == 0) == success, (scenario, result.stdout, result.stderr)
            assert not (case / "published").exists(), scenario
    print("uploaded release asset verification tests passed")


if __name__ == "__main__":
    main()
