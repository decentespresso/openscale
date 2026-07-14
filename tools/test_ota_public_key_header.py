import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "write_ota_public_key_header.py"


def load_module():
    spec = importlib.util.spec_from_file_location("write_ota_public_key_header", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_public_key(path, value):
    path.write_text(
        f"-----BEGIN PUBLIC KEY-----\n{value}\n-----END PUBLIC KEY-----\n",
        encoding="utf-8",
    )


class OtaPublicKeyHeaderTest(unittest.TestCase):
    def test_writes_three_distinct_public_keys(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = tuple(directory / f"key-{index}.pem" for index in range(1, 4))
            module.OUTPUT = directory / "ota_public_key.h"
            for index, path in enumerate(module.KEY_FILES, 1):
                write_public_key(path, f"key-{index}")

            module.main()

            text = module.OUTPUT.read_text(encoding="utf-8")
            for index in range(1, 4):
                self.assertIn(f"#define HDS_OTA_MANIFEST_PUBLIC_KEY_{index}_PEM", text)
                self.assertIn(f"\\nkey-{index}\\n", text)

    def test_rejects_missing_public_key(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = tuple(directory / f"key-{index}.pem" for index in range(1, 4))
            write_public_key(module.KEY_FILES[0], "key-1")
            write_public_key(module.KEY_FILES[1], "key-2")

            with self.assertRaisesRegex(SystemExit, "missing OTA public key file"):
                module.main()

    def test_rejects_duplicate_public_keys(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            directory = Path(temp_dir)
            module.KEY_FILES = tuple(directory / f"key-{index}.pem" for index in range(1, 4))
            for path in module.KEY_FILES:
                write_public_key(path, "same-key")

            with self.assertRaisesRegex(SystemExit, "OTA public keys must be distinct"):
                module.main()


if __name__ == "__main__":
    unittest.main()
