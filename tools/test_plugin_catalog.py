import json
from pathlib import Path
import tempfile
from unittest.mock import patch

import configure_custom_build as customBuild


def writeConfig(root, features, plugins, firmwareRef="main"):
    path = root / "custom-build.json"
    path.write_text(json.dumps({
        "firmware_ref": firmwareRef,
        "features": features,
        "plugins": plugins,
    }), encoding="utf-8")
    return path


def pluginManifest(pluginId="code-plugin", **overrides):
    manifest = {
        "schema": 2,
        "id": pluginId,
        "name": "Code Plugin",
        "description": "Test plugin with an approved patch.",
        "tooltip": "Used to verify the plugin package contract.",
        "version": "1.0.0",
        "firmware_refs": ["main"],
        "requires": [],
        "conflicts": [],
        "patches": {},
        "assets": [],
        "budget": {
            "firmware_flash_bytes": 0,
            "static_ram_bytes": 0,
            "littlefs_bytes": 0,
        },
    }
    return {**manifest, **overrides}


def writePlugin(root, manifest, files=None):
    pluginDir = root / "plugins" / manifest["id"]
    pluginDir.mkdir(parents=True, exist_ok=True)
    (pluginDir / "plugin.json").write_text(json.dumps(manifest), encoding="utf-8")
    for relativePath, contents in (files or {}).items():
        path = pluginDir / relativePath
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")


def assertRejected(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError("invalid plugin input accepted")


def testTemporaryPluginValidation():
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        valid = pluginManifest(
            firmware_refs=["v3.1.13"],
            patches={"v3.1.13": "patches/v3.1.13.patch"},
        )
        writePlugin(root, valid, {"patches/v3.1.13.patch": "diff --git a/a b/a\n"})
        with patch.object(customBuild, "ROOT", root), patch.object(
            customBuild, "FIRMWARE_REFS", ("main", "v3.1.13")
        ):
            manifest, assets, patches = customBuild.loadPlugin("code-plugin", "v3.1.13")
            assert manifest["version"] == "1.0.0"
            assert assets == []
            assert patches["v3.1.13"].is_file()
            assert customBuild.resolveConfiguration(
                writeConfig(root, [], ["code-plugin"], "v3.1.13")
            )["plugins"][0]["id"] == "code-plugin"
            assertRejected(lambda: customBuild.resolveConfiguration(
                writeConfig(root, [], ["missing-plugin"])
            ))
            assertRejected(lambda: customBuild.resolveConfiguration(
                writeConfig(root, [], [], "v9.0.0")
            ))

            writePlugin(root, pluginManifest(patches={"main": "../escape.patch"}))
            assertRejected(lambda: customBuild.loadPlugin("code-plugin", "main"))

            duplicateAssets = [
                {"source": "assets/one.html", "target": "plugins/example/index.html"},
                {"source": "assets/two.html", "target": "plugins/example/index.html"},
            ]
            writePlugin(root, pluginManifest(assets=duplicateAssets), {
                "assets/one.html": "one",
                "assets/two.html": "two",
            })
            assertRejected(lambda: customBuild.loadPlugin("code-plugin", "main"))

            writePlugin(root, pluginManifest(firmware_refs=["unknown-ref"]))
            assertRejected(lambda: customBuild.loadPlugin("code-plugin"))

            writePlugin(root, pluginManifest(conflicts=["missing-plugin"]))
            assertRejected(customBuild.loadPluginCatalog)

            invalidBudget = {
                "firmware_flash_bytes": True,
                "static_ram_bytes": 0,
                "littlefs_bytes": 0,
            }
            writePlugin(root, pluginManifest(budget=invalidBudget))
            assertRejected(lambda: customBuild.loadPlugin("code-plugin"))

            writePlugin(root, pluginManifest(conflicts=["other-plugin"]))
            writePlugin(root, pluginManifest(pluginId="other-plugin"))
            assertRejected(lambda: customBuild.resolveConfiguration(
                writeConfig(root, [], ["code-plugin", "other-plugin"])
            ))


def main():
    pluginIds = [
        path.parent.name
        for path in sorted((customBuild.ROOT / "plugins").glob("*/plugin.json"))
    ]
    assert len(pluginIds) == len(set(pluginIds))
    assert all(customBuild.ID_PATTERN.fullmatch(pluginId) for pluginId in pluginIds)
    for pluginId in pluginIds:
        customBuild.loadPlugin(pluginId, "main")
    generatedCatalog = customBuild.buildBrowserCatalog()
    pageCatalog = json.loads(
        (customBuild.ROOT / "docs" / "custom-build" / "catalog.json").read_text(encoding="utf-8")
    )
    serviceCatalog = json.loads(
        (customBuild.ROOT / "docs" / "custom-build" / "service-catalog.json").read_text(
            encoding="utf-8"
        )
    )
    assert pageCatalog == generatedCatalog, (
        "browser catalog is stale; run python tools/configure_custom_build.py "
        "--catalog-output docs/custom-build/catalog.json "
        "--service-catalog-output docs/custom-build/service-catalog.json"
    )
    assert serviceCatalog == customBuild.buildServiceCatalog()
    pageRoot = customBuild.ROOT / "docs" / "custom-build"
    indexPage = (pageRoot / "index.html").read_text(encoding="utf-8")
    appScript = (pageRoot / "app.js").read_text(encoding="utf-8")
    assert 'src="app.js?v=4"' in indexPage
    assert 'href="styles.css?v=3"' in indexPage
    assert 'id="request-build"' in indexPage
    assert "catalog-data" not in indexPage
    assert 'fetch("catalog.json"' in appScript
    assert "openscale-custom-builds.odevstudio.workers.dev" in appScript
    assert "catalog.features.filter(item => !item.hidden)" in appScript
    grinderFeature = next(feature for feature in generatedCatalog["features"] if feature["id"] == "grinder")
    grindByWeight = next(plugin for plugin in generatedCatalog["plugins"] if plugin["id"] == "grind-by-weight")
    assert grinderFeature["name"] == "Grind by weight"
    assert grinderFeature["hidden"] is True
    assert grindByWeight["name"] == "Grind by weight"
    assert grindByWeight["requires"] == ["grinder"]
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        configuration = customBuild.resolveConfiguration(
            writeConfig(Path(temporaryDirectory), ["pull-ota"], ["hello-web"])
        )
        assert set(customBuild.FEATURES) >= {"wifi", "webserver", "littlefs", "pull-ota"}
        hello = next(plugin for plugin in configuration["plugins"] if plugin["id"] == "hello-web")
        assert set(hello["requires"]) == {"wifi", "webserver", "littlefs"}
        assert hello["patches"] == {}
        assert {"wifi", "webserver", "littlefs", "pull-ota"}.issubset(configuration["features"])
        assert all(source.is_file() for source, _ in configuration["assets"])
        assert all(not target.is_absolute() and ".." not in target.parts for _, target in configuration["assets"])
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        for feature, (_, dependencies) in customBuild.FEATURES.items():
            resolvedFeature = customBuild.resolveConfiguration(writeConfig(root, [feature], []))
            assert set(dependencies).issubset(resolvedFeature["features"])
        wifi = customBuild.resolveConfiguration(writeConfig(root, ["wifi"], []))
        assert wifi["features"] == ["webserver", "wifi"]
        noFeatures = customBuild.resolveConfiguration(writeConfig(root, [], []))
        assert noFeatures["features"] == []
        pullOnly = customBuild.resolveConfiguration(writeConfig(root, ["pull-ota"], []))
        assert {"pull-ota", "wifi"}.issubset(pullOnly["features"])
        assert "littlefs" not in pullOnly["features"]
        assert "webserver" in pullOnly["features"]
        assertRejected(lambda: customBuild.resolveConfiguration(writeConfig(root, [], ["../bad"])))
        resolved = customBuild.resolveConfiguration(writeConfig(root, [], ["hello-web"]))
        assert "littlefs" in resolved["features"]
        grindByWeight = customBuild.resolveConfiguration(writeConfig(root, [], ["grind-by-weight"]))
        assert {"grinder", "wifi", "mdns"}.issubset(grindByWeight["features"])
    testTemporaryPluginValidation()
    print("plugin catalog tests passed")


if __name__ == "__main__":
    main()
