#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path

import configure_custom_build as custom


ROOT = Path(__file__).resolve().parents[1]


class PluginCatalogTest(unittest.TestCase):
    def test_catalog_and_dependencies(self):
        catalog = custom.load_plugin_catalog(ROOT)
        self.assertEqual(list(catalog), ["hello-web"])
        manifest, plugin_root = catalog["hello-web"]
        self.assertEqual(manifest["id"], "hello-web")
        self.assertTrue((plugin_root / "hello.html").is_file())
        resolved = custom.resolve_configuration(ROOT, ROOT / "custom-build.json")
        self.assertTrue({"wifi", "webserver", "littlefs"}.issubset(resolved["features"]))
        self.assertIn("pull-ota", resolved["features"])

    def test_assets_are_safe_and_unique(self):
        targets = set()
        for manifest, plugin_root in custom.load_plugin_catalog(ROOT).values():
            for asset in manifest["assets"]:
                source = custom.safe_relative_path(asset["source"], "asset source")
                target = custom.safe_relative_path(asset["target"], "asset target")
                self.assertTrue((plugin_root / source).is_file())
                self.assertNotIn(target.as_posix(), targets)
                targets.add(target.as_posix())

    def test_invalid_plugin_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "custom-build.json"
            path.write_text(json.dumps({
                "firmware_ref": "main",
                "features": [],
                "plugins": ["../hello-web"],
            }), encoding="utf-8")
            with self.assertRaises(custom.ConfigurationError):
                custom.resolve_configuration(ROOT, path)

    def test_hello_web_cannot_finish_without_littlefs(self):
        catalog = custom.load_plugin_catalog(ROOT)
        manifest, plugin_root = catalog["hello-web"]
        resolved = custom.resolve_features([], [(manifest, plugin_root)])
        self.assertIn("littlefs", resolved)
        resolved.remove("littlefs")
        self.assertFalse(set(manifest["requires"]).issubset(resolved))

    def test_json_is_valid(self):
        with (ROOT / "custom-build.json").open(encoding="utf-8") as handle:
            json.load(handle)
        for manifest_path in ROOT.glob("plugins/*/plugin.json"):
            with manifest_path.open(encoding="utf-8") as handle:
                json.load(handle)


if __name__ == "__main__":
    unittest.main()
