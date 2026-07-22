import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "write_ota_public_key_header.py"
PLATFORMIO_INI = ROOT / "platformio.ini"
PREBUILD_SCRIPT = ROOT / "ota_public_key_header.py"


def load_module():
    spec = importlib.util.spec_from_file_location("write_ota_public_key_header", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PUBLIC_KEYS = tuple(
    ROOT / "keys" / "ota" / f"hds_ota_manifest_public_key_{index}.pem"
    for index in range(1, 4)
)


class OtaPublicKeyHeaderTest(unittest.TestCase):
    def test_writes_three_distinct_public_keys(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = PUBLIC_KEYS
            module.OUTPUT = directory / "ota_public_key.h"

            module.main()

            text = module.OUTPUT.read_text(encoding="utf-8")
            for index in range(1, 4):
                self.assertIn(f"#define HDS_OTA_MANIFEST_PUBLIC_KEY_{index}_PEM", text)
            self.assertEqual(text.count("-----BEGIN PUBLIC KEY-----"), 3)

    def test_rejects_missing_public_key(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = tuple(directory / f"key-{index}.pem" for index in range(1, 4))
            module.KEY_FILES[0].write_bytes(PUBLIC_KEYS[0].read_bytes())
            module.KEY_FILES[1].write_bytes(PUBLIC_KEYS[1].read_bytes())

            with self.assertRaisesRegex(SystemExit, "missing OTA public key file"):
                module.main()

    def test_rejects_malformed_public_key(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = tuple(directory / f"key-{index}.pem" for index in range(1, 4))
            module.KEY_FILES[0].write_bytes(PUBLIC_KEYS[0].read_bytes())
            module.KEY_FILES[1].write_bytes(PUBLIC_KEYS[1].read_bytes())
            module.KEY_FILES[2].write_text(
                "-----BEGIN PUBLIC KEY-----\nkey-3\n-----END PUBLIC KEY-----\n",
                encoding="utf-8",
            )

            with self.assertRaisesRegex(SystemExit, "invalid OTA public key file"):
                module.main()

    def test_rejects_differently_encoded_duplicate_public_keys(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            duplicate = directory / "duplicate.pem"
            subprocess.run(
                [
                    module.openssl_path(),
                    "rsa",
                    "-pubin",
                    "-in",
                    str(PUBLIC_KEYS[0]),
                    "-RSAPublicKey_out",
                    "-out",
                    str(duplicate),
                ],
                check=True,
                capture_output=True,
            )
            module.KEY_FILES = (PUBLIC_KEYS[0], duplicate, PUBLIC_KEYS[1])

            with self.assertRaisesRegex(SystemExit, "OTA public keys must be distinct"):
                module.main()

    def test_requires_openssl(self):
        module = load_module()
        module.KEY_FILES = PUBLIC_KEYS
        with mock.patch.object(module.shutil, "which", return_value=None), mock.patch.dict(
            module.os.environ, {"OPENSSL": "", "ProgramFiles": ""}
        ):
            with self.assertRaisesRegex(SystemExit, "OpenSSL is required"):
                module.main()

    def test_finds_git_for_windows_openssl(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            executable = Path(temp_dir) / "Git" / "usr" / "bin" / "openssl.exe"
            executable.parent.mkdir(parents=True)
            executable.touch()
            with mock.patch.object(module.shutil, "which", return_value=None), mock.patch.dict(
                module.os.environ, {"OPENSSL": "", "ProgramFiles": temp_dir}
            ):
                self.assertEqual(module.openssl_path(), str(executable))

    def test_platformio_runs_public_key_generator(self):
        self.assertIn(
            "pre:ota_public_key_header.py",
            PLATFORMIO_INI.read_text(encoding="utf-8"),
        )
        self.assertIn(
            'runpy.run_path("tools/write_ota_public_key_header.py", run_name="__main__")',
            PREBUILD_SCRIPT.read_text(encoding="utf-8"),
        )


if __name__ == "__main__":
    unittest.main()
