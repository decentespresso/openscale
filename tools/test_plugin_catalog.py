import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "configure_custom_build.py"
SPEC = importlib.util.spec_from_file_location("configure_custom_build", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class PluginCatalogTest(unittest.TestCase):
    def createProject(self, root):
        project = Path(root)
        (project / "web_apps").mkdir(parents=True)
        (project / "web_apps" / "index.html").write_text(
            "<!doctype html><title>OpenScale</title>\n", encoding="utf-8"
        )
        shutil.copytree(ROOT / "plugins", project / "plugins")
        return project

    def writeSelection(self, path, features=None, plugins=None):
        value = {
            "firmware_ref": "main",
            "features": features or [],
            "plugins": plugins or [],
        }
        Path(path).write_text(json.dumps(value), encoding="utf-8")

    def treeDigest(self, root):
        digest = hashlib.sha256()
        root = Path(root)
        for path in sorted(item for item in root.rglob("*") if item.is_file()):
            digest.update(path.relative_to(root).as_posix().encode("utf-8"))
            digest.update(b"\0")
            digest.update(path.read_bytes())
            digest.update(b"\0")
        return digest.hexdigest()

    def testCatalogAndCorrectedPullOtaDependencies(self):
        catalog = MODULE.load_catalog(ROOT)
        self.assertEqual(tuple(catalog), ("hello-web",))
        plugin = catalog["hello-web"]
        self.assertEqual(plugin.version, "1.0.0")
        self.assertEqual(plugin.requires, ("wifi", "webserver", "littlefs"))
        self.assertEqual(
            tuple(target.as_posix() for _, target in plugin.assets),
            ("plugins/hello/index.html",),
        )

        resolved, _ = MODULE.resolve_selection(ROOT / "custom-build.json", ROOT)
        self.assertEqual(resolved.features, ("wifi", "pull_ota"))
        self.assertNotIn("webserver", resolved.features)
        self.assertNotIn("littlefs", resolved.features)
        self.assertNotIn("elegant_ota", resolved.features)

        pull_ota = (ROOT / "include" / "pull_ota.h").read_text(encoding="utf-8")
        self.assertIn("setupWifi();", pull_ota)
        self.assertNotIn("wifi_init(", pull_ota)
        self.assertIn(
            'pullOtaParseAsset(root["littlefs"], manifest.littlefs, true)',
            pull_ota,
        )
        self.assertNotIn('#include "webserver.h"', pull_ota)
        self.assertNotIn('#include "wifi_ota.h"', pull_ota)
        self.assertNotIn("HDS_FEATURE_PULL_OTA_FILESYSTEM", pull_ota)

    def testPagesCatalogMatchesBuildCatalog(self):
        page_catalog = json.loads(
            (ROOT / "docs" / "custom-build" / "catalog.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(page_catalog["schema"], 1)
        self.assertEqual(
            tuple(page_catalog["firmware_refs"]), MODULE.SUPPORTED_FIRMWARE_REFS
        )
        self.assertEqual(page_catalog["defaults"], {"features": ["pull_ota"], "plugins": []})
        self.assertEqual(
            {item["id"]: tuple(item["requires"]) for item in page_catalog["features"]},
            MODULE.FEATURE_DEPENDENCIES,
        )
        plugins = MODULE.load_catalog(ROOT)
        self.assertEqual(
            {
                item["id"]: (
                    item["version"],
                    tuple(item["firmware_refs"]),
                    tuple(item["requires"]),
                    tuple(item["conflicts"]),
                )
                for item in page_catalog["plugins"]
            },
            {
                plugin_id: (
                    plugin.version,
                    plugin.firmware_refs,
                    plugin.requires,
                    plugin.conflicts,
                )
                for plugin_id, plugin in plugins.items()
            },
        )

    def testPluginResolutionAndPreparationAreDeterministic(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            project = self.createProject(Path(temporaryDirectory) / "project")
            selection = project / "selection.json"
            self.writeSelection(selection, plugins=["hello-web"])

            resolved, plugins = MODULE.resolve_selection(selection, project)
            self.assertEqual(
                resolved.features, ("wifi", "webserver", "littlefs")
            )
            MODULE.validate_resolved(resolved.features, resolved.plugins, plugins)

            first = project / "generated-first"
            second = project / "generated-second"
            MODULE.prepare_build(selection, project, first)
            MODULE.prepare_build(selection, project, second)
            self.assertEqual(self.treeDigest(first), self.treeDigest(second))
            self.assertTrue((first / "data" / "index.html").is_file())
            hello = first / "data" / "plugins" / "hello" / "index.html"
            self.assertIn("Hello from an OpenScale plugin", hello.read_text(encoding="utf-8"))

            header = (first / "include" / "generated_features.h").read_text(
                encoding="utf-8"
            )
            for macro in MODULE.FEATURE_MACROS.values():
                self.assertRegex(header, rf"#define {macro} [01]\n")
            self.assertIn("#define HDS_FEATURE_PULL_OTA 0", header)
            self.assertIn("#define HDS_ENABLE_GRINDER 0", header)

    def testManifestTreatsGeneratedLittleFsAsASeparateArtifact(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            build = Path(temporaryDirectory) / "build"
            build.mkdir()
            for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"):
                (build / name).write_bytes(name.encode("utf-8"))
            build_log = Path(temporaryDirectory) / "platformio.log"
            build_log.write_text(
                "RAM: [=         ] 10.0% (used 32768 bytes from 327680 bytes)\n"
                "Flash: [===       ] 30.0% (used 589824 bytes from 1966080 bytes)\n",
                encoding="utf-8",
            )
            output = Path(temporaryDirectory) / "build-manifest.json"
            MODULE.write_manifest(
                ROOT / "custom-build.json",
                ROOT,
                build,
                build_log,
                output,
                "0123456789abcdef",
                "esp32s3-custom",
            )
            manifest = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(
                [artifact["name"] for artifact in manifest["artifacts"]],
                ["firmware.bin", "bootloader.bin", "partitions.bin", "littlefs.bin"],
            )
            self.assertNotIn("littlefs", manifest["features"])
            self.assertEqual(
                manifest["measured_sizes"],
                {
                    "firmware_flash_bytes": 589824,
                    "littlefs_bytes": len("littlefs.bin"),
                    "static_ram_bytes": 32768,
                },
            )

    def testRejectsDuplicateJsonKeys(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            selection = Path(temporaryDirectory) / "selection.json"
            selection.write_text(
                '{"firmware_ref":"main","features":[],"features":[],"plugins":[]}',
                encoding="utf-8",
            )
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.resolve_selection(selection, ROOT)

    def testRejectsDestructiveOutputRoots(self):
        with self.assertRaises(MODULE.ConfigurationError):
            MODULE.prepare_build(ROOT / "custom-build.json", ROOT, ROOT)
        with self.assertRaises(MODULE.ConfigurationError):
            MODULE.prepare_build(ROOT / "custom-build.json", ROOT, ROOT.parent)

    def testRejectsUnsafeCatalogEntriesAndMissingRequirements(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            project = self.createProject(Path(temporaryDirectory) / "project")
            selection = project / "selection.json"
            self.writeSelection(selection, plugins=["../hello"])
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.resolve_selection(selection, project)

            pluginPath = project / "plugins" / "hello-web" / "plugin.json"
            value = json.loads(pluginPath.read_text(encoding="utf-8"))
            value["id"] = "../hello"
            pluginPath.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.load_catalog(project)

        with tempfile.TemporaryDirectory() as temporaryDirectory:
            project = self.createProject(Path(temporaryDirectory) / "project")
            pluginPath = project / "plugins" / "hello-web" / "plugin.json"
            value = json.loads(pluginPath.read_text(encoding="utf-8"))
            value["assets"][0]["target"] = "../index.html"
            pluginPath.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.load_catalog(project)

        with tempfile.TemporaryDirectory() as temporaryDirectory:
            project = self.createProject(Path(temporaryDirectory) / "project")
            (project / "plugins" / "hello-web" / "hello.html").unlink()
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.load_catalog(project)

        catalog = MODULE.load_catalog(ROOT)
        with self.assertRaises(MODULE.ConfigurationError):
            MODULE.validate_resolved(
                ("wifi", "webserver"), ("hello-web",), catalog
            )

    def testRejectsAssetTargetCollisions(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            project = self.createProject(Path(temporaryDirectory) / "project")
            source = project / "plugins" / "hello-web"
            duplicate = project / "plugins" / "other-web"
            shutil.copytree(source, duplicate)
            value = json.loads((duplicate / "plugin.json").read_text(encoding="utf-8"))
            value["id"] = "other-web"
            value["name"] = "Other Web"
            (duplicate / "plugin.json").write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(MODULE.ConfigurationError):
                MODULE.load_catalog(project)


if __name__ == "__main__":
    unittest.main()
