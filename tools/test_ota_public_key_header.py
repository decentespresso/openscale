import importlib.util
import os
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


class OtaPublicKeyHeaderTest(unittest.TestCase):
    def test_writes_escaped_public_key_macro(self):
        module = load_module()
        old_pem = os.environ.get("HDS_OTA_MANIFEST_PUBLIC_KEY_PEM")
        pem = "-----BEGIN PUBLIC KEY-----\nabc\n-----END PUBLIC KEY-----"
        with tempfile.TemporaryDirectory() as temp_dir:
            module.OUTPUT = Path(temp_dir) / "ota_public_key.h"
            os.environ["HDS_OTA_MANIFEST_PUBLIC_KEY_PEM"] = pem
            try:
                module.main()
            finally:
                if old_pem is None:
                    os.environ.pop("HDS_OTA_MANIFEST_PUBLIC_KEY_PEM", None)
                else:
                    os.environ["HDS_OTA_MANIFEST_PUBLIC_KEY_PEM"] = old_pem
            text = module.OUTPUT.read_text(encoding="utf-8")
            self.assertIn("#define HDS_OTA_MANIFEST_PUBLIC_KEY_PEM", text)
            self.assertIn("\\nabc\\n", text)

    def test_rejects_missing_public_key(self):
        module = load_module()
        old_pem = os.environ.pop("HDS_OTA_MANIFEST_PUBLIC_KEY_PEM", None)
        try:
            with self.assertRaises(SystemExit):
                module.main()
        finally:
            if old_pem is not None:
                os.environ["HDS_OTA_MANIFEST_PUBLIC_KEY_PEM"] = old_pem


if __name__ == "__main__":
    unittest.main()
