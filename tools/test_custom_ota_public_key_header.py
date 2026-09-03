import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
PREBUILD = ROOT / "custom_ota_public_key_header.py"


def opensslPath():
    executable = shutil.which(os.environ.get("OPENSSL", "openssl"))
    if executable:
        return executable
    git = shutil.which("git")
    if git:
        candidate = Path(git).resolve().parent.parent / "usr" / "bin" / "openssl.exe"
        if candidate.is_file():
            return str(candidate)
    raise AssertionError("OpenSSL is required")


def canonicalPublicKey(path):
    return subprocess.run(
        [opensslPath(), "pkey", "-pubin", "-in", str(path), "-pubout", "-outform", "DER"],
        check=True,
        capture_output=True,
    ).stdout


def main():
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        publicKeys = []
        for index in range(1, 3):
            privateKey = root / f"private-{index}.pem"
            publicKey = root / f"public-{index}.pem"
            subprocess.run(
                [opensslPath(), "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(privateKey)],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                [opensslPath(), "pkey", "-in", str(privateKey), "-pubout", "-out", str(publicKey)],
                check=True,
                capture_output=True,
            )
            publicKeys.append(publicKey)

        class PlatformIoEnvironment:
            def IsCleanTarget(self):
                return False

            def subst(self, _):
                return str(ROOT / "tools" / "write_custom_ota_public_key_header.py")

        previousDirectory = Path.cwd()
        try:
            os.chdir(root)
            environment = {
                **{
                    f"HDS_CUSTOM_OTA_PUBLIC_KEY_{index}_FILE": str(publicKey)
                    for index, publicKey in enumerate(publicKeys, 1)
                },
                "PATH": os.environ.get("PATH", ""),
                **({"OPENSSL": os.environ["OPENSSL"]} if os.environ.get("OPENSSL") else {}),
            }
            with mock.patch.dict(os.environ, environment, clear=True):
                exec(PREBUILD.read_text(encoding="utf-8"), {
                    "Import": lambda _: None,
                    "env": PlatformIoEnvironment(),
                })
        finally:
            os.chdir(previousDirectory)

        header = (root / ".pio.nosync/generated/include/custom_ota_public_key.h").read_text(encoding="utf-8")
        for index, publicKey in enumerate(publicKeys, 1):
            match = re.search(
                rf"#define HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_{index}_PEM (.+)", header
            )
            if not match:
                raise AssertionError(f"custom OTA public key {index} macro was not generated")
            embedded = json.loads(match.group(1))
            if not embedded:
                raise AssertionError(f"custom OTA public key {index} is empty after the PlatformIO pre-build")
            embeddedPath = root / f"embedded-{index}.pem"
            embeddedPath.write_text(embedded, encoding="utf-8")
            if canonicalPublicKey(embeddedPath) != canonicalPublicKey(publicKey):
                raise AssertionError(f"PlatformIO pre-build embedded the wrong custom OTA public key {index}")

    print("custom OTA public key header tests passed")


if __name__ == "__main__":
    main()
