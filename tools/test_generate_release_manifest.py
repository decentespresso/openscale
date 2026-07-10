import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "generate_release_manifest.py"


def load_module():
    spec = importlib.util.spec_from_file_location("generate_release_manifest", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class GenerateReleaseManifestTest(unittest.TestCase):
    def test_manifest_contains_release_assets(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            output_dir = root / "release-files"
            build_dir.mkdir()
            output_dir.mkdir()
            firmware = b"firmware image"
            littlefs = b"littlefs image"
            (build_dir / "firmware.bin").write_bytes(firmware)
            (build_dir / "littlefs.bin").write_bytes(littlefs)

            manifest = module.build_manifest(
                build_dir=build_dir,
                tag="v3.1.0",
                repository="decentespresso/openscale",
                model="hds",
                min_from="3.0.0",
                pcb="PCB: 8.1",
                chip="esp32s3",
                environment="esp32s3",
                flash_size=8388608,
                partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                app_partition_min_size=3342336,
                fs_partition_label="spiffs",
                fs_partition_size=1572864,
                fs_schema=1,
            )
            module.write_manifest(manifest, output_dir / "manifest.json")

            data = json.loads((output_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(data["model"], "hds")
            self.assertEqual(data["pcb"], "PCB: 8.1")
            self.assertEqual(data["chip"], "esp32s3")
            self.assertEqual(data["environment"], "esp32s3")
            self.assertEqual(data["flash_size"], 8388608)
            self.assertEqual(data["partition_schema"], "esp32s3-default-8mb-ota-spiffs-1536k")
            self.assertEqual(data["app_partition_min_size"], 3342336)
            self.assertEqual(data["fs_partition_label"], "spiffs")
            self.assertEqual(data["fs_partition_size"], 1572864)
            self.assertEqual(data["fs_schema"], 1)
            self.assertEqual(data["version"], "3.1.0")
            self.assertEqual(data["min_from"], "3.0.0")
            self.assertEqual(
                data["release_notes_url"],
                "https://github.com/decentespresso/openscale/releases/tag/v3.1.0",
            )
            self.assertEqual(
                data["firmware"]["url"],
                "https://github.com/decentespresso/openscale/releases/download/v3.1.0/firmware.bin",
            )
            self.assertEqual(data["firmware"]["size"], len(firmware))
            self.assertEqual(
                data["firmware"]["sha256"],
                "1df2f3853d10a305aa52d36fd4a03f5721d7ce7daef6f7e5e8d51074d31361f1",
            )
            self.assertEqual(
                data["littlefs"]["url"],
                "https://github.com/decentespresso/openscale/releases/download/v3.1.0/littlefs.bin",
            )
            self.assertEqual(data["littlefs"]["size"], len(littlefs))
            self.assertEqual(
                data["littlefs"]["sha256"],
                "a55566d7e72426d1adc2aae7f4e7b25527fbe14fe49359db715f0976cc993871",
            )
            self.assertTrue(data["littlefs"]["required"])
            self.assertNotIn("releases", data)

    def test_littlefs_is_required(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "firmware.bin").write_bytes(b"firmware image")

            with self.assertRaises(FileNotFoundError):
                module.build_manifest(
                    build_dir=build_dir,
                    tag="3.1.0",
                    repository="decentespresso/openscale",
                    model="hds",
                    min_from="3.0.0",
                    pcb=None,
                    chip="esp32s3",
                    environment="esp32s3",
                    flash_size=8388608,
                    partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                    app_partition_min_size=3342336,
                    fs_partition_label="spiffs",
                    fs_partition_size=1572864,
                    fs_schema=1,
                )

    def test_rejects_versions_the_device_rejects(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "firmware.bin").write_bytes(b"firmware image")

            with self.assertRaises(ValueError):
                module.build_manifest(
                    build_dir=build_dir,
                    tag="v3.1.0-preview.1",
                    repository="decentespresso/openscale",
                    model="hds",
                    min_from="3.0.0",
                    pcb=None,
                    chip="esp32s3",
                    environment="esp32s3",
                    flash_size=8388608,
                    partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                    app_partition_min_size=3342336,
                    fs_partition_label="spiffs",
                    fs_partition_size=1572864,
                    fs_schema=1,
                )

    def test_release_catalog_is_optional(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "firmware.bin").write_bytes(b"new firmware")
            (build_dir / "littlefs.bin").write_bytes(b"new littlefs")
            previous = {
                "model": "hds",
                "version": "3.0.9",
                "min_from": "3.0.0",
                "release_notes_url": "https://github.com/decentespresso/openscale/releases/tag/v3.0.9",
                "chip": "esp32s3",
                "environment": "esp32s3",
                "flash_size": 8388608,
                "partition_schema": "esp32s3-default-8mb-ota-spiffs-1536k",
                "app_partition_min_size": 3342336,
                "fs_partition_label": "spiffs",
                "fs_partition_size": 1572864,
                "fs_schema": 1,
                "firmware": {
                    "url": "https://github.com/decentespresso/openscale/releases/download/v3.0.9/firmware.bin",
                    "size": 12,
                    "sha256": "0" * 64,
                },
            }

            latest = module.build_manifest(
                build_dir=build_dir,
                tag="v3.1.0",
                repository="decentespresso/openscale",
                model="hds",
                min_from="3.0.0",
                pcb=None,
                chip="esp32s3",
                environment="esp32s3",
                flash_size=8388608,
                partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                app_partition_min_size=3342336,
                fs_partition_label="spiffs",
                fs_partition_size=1572864,
                fs_schema=1,
            )
            catalog = module.build_catalog_manifest(latest, [previous])

            self.assertEqual(catalog["version"], "3.1.0")
            self.assertEqual([release["version"] for release in catalog["releases"]], ["3.1.0", "3.0.9"])

    def test_release_catalog_dedupes_sorts_and_applies_production_floor(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "firmware.bin").write_bytes(b"new firmware")
            (build_dir / "littlefs.bin").write_bytes(b"new littlefs")
            previous = {
                "releases": [
                    {
                        "model": "hds",
                        "version": "3.1.14",
                        "chip": "esp32s3",
                        "environment": "esp32s3",
                    },
                    {
                        "model": "hds",
                        "version": "3.1.13",
                        "chip": "esp32s3",
                        "environment": "esp32s3",
                    },
                    {
                        "model": "hds",
                        "version": "3.1.12",
                        "chip": "esp32s3",
                        "environment": "esp32s3",
                    },
                ]
            }
            latest = module.build_manifest(
                build_dir=build_dir,
                tag="v3.1.14",
                repository="decentespresso/openscale",
                model="hds",
                min_from="3.0.0",
                pcb=None,
                chip="esp32s3",
                environment="esp32s3",
                flash_size=8388608,
                partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                app_partition_min_size=3342336,
                fs_partition_label="spiffs",
                fs_partition_size=1572864,
                fs_schema=1,
            )
            catalog = module.build_catalog_manifest(latest, [previous], min_version="v3.1.13")

            self.assertEqual([release["version"] for release in catalog["releases"]], ["3.1.14", "3.1.13"])

    def test_release_catalog_skips_preview_entries(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            build_dir = Path(temp_dir)
            (build_dir / "firmware.bin").write_bytes(b"new firmware")
            (build_dir / "littlefs.bin").write_bytes(b"new littlefs")
            previous = {
                "releases": [
                    {
                        "model": "hds",
                        "version": "3.1.15-rc.1",
                        "chip": "esp32s3",
                        "environment": "esp32s3",
                    },
                    {
                        "model": "hds",
                        "version": "3.1.13",
                        "chip": "esp32s3",
                        "environment": "esp32s3",
                    },
                ]
            }
            latest = module.build_manifest(
                build_dir=build_dir,
                tag="v3.1.14",
                repository="decentespresso/openscale",
                model="hds",
                min_from="3.0.0",
                pcb=None,
                chip="esp32s3",
                environment="esp32s3",
                flash_size=8388608,
                partition_schema="esp32s3-default-8mb-ota-spiffs-1536k",
                app_partition_min_size=3342336,
                fs_partition_label="spiffs",
                fs_partition_size=1572864,
                fs_schema=1,
            )
            catalog = module.build_catalog_manifest(latest, [previous], min_version="v3.1.13")

            self.assertEqual([release["version"] for release in catalog["releases"]], ["3.1.14", "3.1.13"])

    def test_release_catalog_caps_entries_and_serialized_size(self):
        module = load_module()
        latest = {
            "model": "hds",
            "version": "3.1.60",
            "chip": "esp32s3",
            "environment": "esp32s3",
        }
        previous = {
            "releases": [
                {
                    "model": "hds",
                    "version": f"3.1.{version}",
                    "chip": "esp32s3",
                    "environment": "esp32s3",
                }
                for version in range(13, 60)
            ]
        }

        catalog = module.build_catalog_manifest(latest, [previous], min_version="v3.1.13")

        self.assertEqual(len(catalog["releases"]), module.MAX_CATALOG_RELEASES)
        self.assertEqual(catalog["releases"][0]["version"], "3.1.60")
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = Path(temp_dir) / "manifest.json"
            module.write_manifest(catalog, manifest_path)
            self.assertLessEqual(manifest_path.stat().st_size, module.MAX_MANIFEST_BYTES)

    def test_release_manifest_rejects_oversized_output(self):
        module = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = Path(temp_dir) / "manifest.json"
            with self.assertRaisesRegex(ValueError, "manifest exceeds 32768 bytes"):
                module.write_manifest(
                    {"release_notes_url": "x" * module.MAX_MANIFEST_BYTES},
                    manifest_path,
                )
            self.assertFalse(manifest_path.exists())


if __name__ == "__main__":
    unittest.main()
