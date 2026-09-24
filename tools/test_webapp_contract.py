import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

from PIL import Image

import configure_custom_build as customBuild


SOURCE_ROOT = Path(__file__).resolve().parents[1]
WORKER_MODULE = (SOURCE_ROOT / "cloudflare" / "custom-build-worker" / "src" / "worker.mjs").as_uri()
COMMIT = "a" * 40


def manifest(pluginId, name=None, **overrides):
    return {
        "schema": 2,
        "id": pluginId,
        "name": name or pluginId,
        "description": "Test plugin",
        "tooltip": "Test plugin",
        "version": "1.0.0",
        "firmware_refs": ["main"],
        "requires": ["littlefs", "webserver"],
        "depends_on": [],
        "conflicts": [],
        "recommends": {"features": [], "plugins": []},
        "patches": {},
        "budget": {"firmware_flash_bytes": 0, "static_ram_bytes": 0, "littlefs_bytes": 0},
        **overrides,
    }


def writePlugin(root, data, files):
    directory = root / "plugins" / data["id"]
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "plugin.json").write_text(json.dumps(data), encoding="utf-8")
    for relative, content in files.items():
        target = directory / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
    return directory


def resolve(root, ids):
    config = root / "selection.json"
    config.write_text(json.dumps({"firmware_ref": "main", "features": [], "plugins": ids}), encoding="utf-8")
    return customBuild.resolveConfiguration(config)


def workerIdentity(catalog, selection):
    script = (
        "import {readFileSync} from 'node:fs';"
        "import {createHash} from 'node:crypto';"
        "const {identityForSelection,canonicalJson}=await import(process.argv[1]);"
        "const {catalog,selection,commit}=JSON.parse(readFileSync(0,'utf8'));"
        "const identity=identityForSelection(catalog,commit,selection);"
        "const hash=createHash('sha256').update(canonicalJson(identity)).digest('hex');"
        "process.stdout.write(JSON.stringify({identity,hash}));"
    )
    result = subprocess.run(
        ["node", "--input-type=module", "-e", script, WORKER_MODULE],
        input=json.dumps({"catalog": catalog, "selection": selection, "commit": COMMIT}),
        text=True,
        capture_output=True,
        check=True,
    )
    return json.loads(result.stdout)


def assertRejected(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError("invalid plugin input accepted")


def testWebapps(buildfs=False):
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        alpha = manifest("alpha", "Alpha & <Beta>")
        writePlugin(root, alpha, {
            "assets/index.html": '<link rel="stylesheet" href="./app.css">',
            "assets/app.css": "body { color: red; }",
        })
        beta = manifest("beta", requires=["littlefs", "websocket"])
        writePlugin(root, beta, {
            "assets/index.html": '<script src="./js/app.js"></script>',
            "assets/js/app.js": "console.log('beta')",
            "assets/shared/beta.json": "{}",
        })
        secondRoot = manifest("second-root")
        writePlugin(root, secondRoot, {
            "assets/index.html": '<link rel="stylesheet" href="./app.css">',
            "assets/app.css": "body { color: blue; }",
        })
        default = manifest("default-web-apps")
        writePlugin(root, default, {"assets/index.html": "DEFAULT ROOT"})
        with patch.object(customBuild, "ROOT", root):
            empty = resolve(root, [])
            emptyStage = root / "empty-stage"
            customBuild.stageAssets(empty, emptyStage)
            assert not (emptyStage / "index.html").exists()

            one = resolve(root, ["alpha"])
            oneStage = root / "one-stage"
            customBuild.stageAssets(one, oneStage)
            assert (oneStage / "apps/alpha/index.html").is_file()
            assert (oneStage / "apps/alpha/app.css").is_file()
            assert not (oneStage / "app.css").exists()
            assert "/apps/alpha/index.html" in (oneStage / "index.html").read_text(encoding="utf-8")

            many = resolve(root, ["beta", "alpha"])
            manyStage = root / "many-stage"
            customBuild.stageAssets(many, manyStage)
            launcher = (manyStage / "index.html").read_text(encoding="utf-8")
            assert "/apps/alpha/index.html" in launcher
            assert "/apps/beta/index.html" in launcher
            assert "Alpha &amp; &lt;Beta&gt;" in launcher
            assert (manyStage / "apps/beta/js/app.js").is_file()
            assert (manyStage / "apps/beta/shared/beta.json").is_file()
            assert not (manyStage / "shared/beta.json").exists()
            registry = json.loads((manyStage / "webapps.json").read_text(encoding="utf-8"))
            assert [app["id"] for app in registry["apps"]] == ["alpha", "beta"]
            twoRoots = resolve(root, ["alpha", "second-root"])
            twoRootsStage = root / "two-roots-stage"
            customBuild.stageAssets(twoRoots, twoRootsStage)
            assert (twoRootsStage / "apps/alpha/app.css").is_file()
            assert (twoRootsStage / "apps/second-root/app.css").is_file()
            assert "/apps/second-root/index.html" in (twoRootsStage / "index.html").read_text(encoding="utf-8")
            if buildfs:
                resolve(root, ["alpha", "beta"])
                environment = {
                    **os.environ,
                    "HDS_CUSTOM_BUILD_CATALOG_ROOT": str(root),
                    "HDS_CUSTOM_BUILD_CONFIG": str(root / "selection.json"),
                }
                subprocess.run(
                    ["pio", "run", "-e", "esp32s3-custom", "-t", "buildfs"],
                    cwd=SOURCE_ROOT, env=environment, check=True,
                )

            withDefault = resolve(root, ["alpha", "default-web-apps"])
            defaultStage = root / "default-stage"
            customBuild.stageAssets(withDefault, defaultStage)
            assert (defaultStage / "index.html").read_text(encoding="utf-8") == "DEFAULT ROOT"
            assert (defaultStage / "apps/alpha/index.html").is_file()

            catalog = customBuild.buildServiceCatalog(SOURCE_ROOT)
            for ids in ([], ["alpha"], ["beta", "alpha"], ["alpha", "second-root"], ["alpha", "default-web-apps"]):
                selection = {"firmware_ref": "main", "features": [], "plugins": ids}
                pythonIdentity = customBuild.combinationInput(resolve(root, ids), COMMIT, SOURCE_ROOT)
                worker = workerIdentity(catalog, selection)
                assert worker["identity"] == pythonIdentity
                assert worker["hash"] == customBuild.combinationHash(pythonIdentity)
            originalHash = customBuild.combinationHash(customBuild.combinationInput(
                resolve(root, ["alpha", "beta"]), COMMIT, SOURCE_ROOT
            ))

            renamed = {**alpha, "name": "Renamed Alpha"}
            writePlugin(root, renamed, {})
            changed = customBuild.combinationInput(resolve(root, ["alpha", "beta"]), COMMIT, SOURCE_ROOT)
            assert customBuild.combinationHash(changed) != originalHash
            writePlugin(root, alpha, {"assets/app.css": "body { color: green; }"})
            changedFile = customBuild.combinationInput(resolve(root, ["alpha", "beta"]), COMMIT, SOURCE_ROOT)
            assert customBuild.combinationHash(changedFile) != originalHash

            writePlugin(root, alpha, {"assets/new.js": "console.log('new')"})
            discovered = resolve(root, ["alpha", "beta"])
            discoveredStage = root / "discovered-stage"
            customBuild.stageAssets(discovered, discoveredStage)
            assert (discoveredStage / "apps/alpha/new.js").is_file()
            assert customBuild.combinationHash(
                customBuild.combinationInput(discovered, COMMIT, SOURCE_ROOT)
            ) != customBuild.combinationHash(changedFile)

            (root / ".gitignore").write_text("plugins/alpha/assets/*.gz\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=root, check=True)
            writePlugin(root, alpha, {"assets/app.js.gz": "generated gzip"})
            assert all(target.suffix != ".gz" for _, target in customBuild.loadPlugin("alpha")[1])
            beforeAddedFile = customBuild.combinationHash(customBuild.combinationInput(
                resolve(root, ["alpha"]), COMMIT, SOURCE_ROOT
            ))
            writePlugin(root, alpha, {"assets/added-after-git.js": "console.log('added')"})
            assert customBuild.combinationHash(customBuild.combinationInput(
                resolve(root, ["alpha"]), COMMIT, SOURCE_ROOT
            )) != beforeAddedFile
            subprocess.run(["git", "add", "-f", "plugins/alpha/assets/app.js.gz"], cwd=root, check=True)
            assertRejected(lambda: customBuild.loadPlugin("alpha"))
            subprocess.run(["git", "rm", "-q", "--cached", "plugins/alpha/assets/app.js.gz"], cwd=root, check=True)

            writePlugin(root, manifest("gamma"), {"assets/shared.txt": "one"})
            writePlugin(root, manifest("delta"), {"assets/shared.txt": "two"})
            assertRejected(lambda: resolve(root, ["gamma", "delta"]))
            writePlugin(root, manifest("missing-features", requires=[]), {"assets/index.html": "app"})
            assertRejected(lambda: resolve(root, ["missing-features"]))
            writePlugin(root, manifest("legacy-webapp"), {"webapp/index.html": "app"})
            assertRejected(lambda: customBuild.loadPlugin("legacy-webapp"))
            writePlugin(root, manifest("legacy-list", assets=[]), {})
            assertRejected(lambda: customBuild.loadPlugin("legacy-list"))
            directoryIndex = writePlugin(root, manifest("directory-index"), {})
            (directoryIndex / "assets" / "index.html").mkdir(parents=True)
            assertRejected(lambda: customBuild.loadPlugin("directory-index"))
            writePlugin(root, manifest("gzip-only"), {
                "assets/index.html": "app", "assets/app.js.gz": "supplied gzip",
            })
            assertRejected(lambda: customBuild.loadPlugin("gzip-only"))
            writePlugin(root, manifest("gzip-sibling"), {
                "assets/index.html": "app",
                "assets/app.js": "source",
                "assets/app.js.gz": "supplied gzip",
            })
            assertRejected(lambda: customBuild.loadPlugin("gzip-sibling"))
            writePlugin(root, manifest("gzip-css"), {
                "assets/index.html": "app", "assets/app.css.gz": "supplied gzip",
            })
            assertRejected(lambda: customBuild.loadPlugin("gzip-css"))
            writePlugin(root, manifest("reserved"), {"assets/webapps.json": "{}"})
            assertRejected(lambda: customBuild.loadPlugin("reserved"))
            writePlugin(root, manifest("reserved-child"), {"assets/webapps.json/child": "{}"})
            assertRejected(lambda: customBuild.loadPlugin("reserved-child"))


def testPresentation():
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        data = manifest("picture", presentation={
            "image": "media/preview.png",
            "image_alt": "Scale dashboard",
            "handbook": "README.md",
        })
        directory = writePlugin(root, data, {"README.md": "# Picture\n"})
        (directory / "media").mkdir()
        Image.new("RGB", (2, 2), "red").save(directory / "media/preview.png")
        with patch.object(customBuild, "ROOT", root), patch.object(customBuild, "DEFAULT_PLUGINS", set()):
            originalService = customBuild.buildServiceCatalog(SOURCE_ROOT)
            originalIdentity = customBuild.combinationInput(resolve(root, ["picture"]), COMMIT, SOURCE_ROOT)
            browser = customBuild.buildBrowserCatalog()
            presentation = browser["plugins"][0]["presentation"]
            assert presentation["image"].startswith("plugin-media/picture/")
            assert presentation["handbook"].startswith("plugin-media/picture/")
            assert presentation["handbook"].endswith(".md")
            output = root / "site" / "catalog.json"
            stale = output.parent / "plugin-media" / "picture" / "stale.png"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"stale")
            customBuild.writeBrowserCatalog(output)
            assert not stale.exists()
            assert (output.parent / presentation["image"]).is_file()
            assert (output.parent / presentation["handbook"]).read_text(encoding="utf-8") == "# Picture\n"

            changed = {**data, "presentation": {**data["presentation"], "image_alt": "New alt"}}
            writePlugin(root, changed, {})
            assert customBuild.buildServiceCatalog(SOURCE_ROOT) == originalService
            assert customBuild.combinationInput(resolve(root, ["picture"]), COMMIT, SOURCE_ROOT) == originalIdentity
            Image.new("RGB", (3, 3), "blue").save(directory / "media/preview.png")
            assert customBuild.buildServiceCatalog(SOURCE_ROOT) == originalService
            assert customBuild.combinationInput(resolve(root, ["picture"]), COMMIT, SOURCE_ROOT) == originalIdentity
            writePlugin(root, {**data, "presentation": {"image": "media/preview.png"}}, {})
            assertRejected(customBuild.buildBrowserCatalog)
            writePlugin(root, {**data, "presentation": {"handbook": "../README.md"}}, {})
            assertRejected(customBuild.buildBrowserCatalog)
            writePlugin(root, {**data, "presentation": {"handbook": "README.pdf"}}, {
                "README.pdf": "not a README",
            })
            assertRejected(customBuild.buildBrowserCatalog)
            writePlugin(root, data, {})
            (directory / "README.md").write_bytes(b"\xff")
            assertRejected(customBuild.buildBrowserCatalog)
            (directory / "README.md").write_text("# Picture\n", encoding="utf-8")
            writePlugin(root, {**data, "presentation": {"image": "media/preview.svg", "image_alt": "SVG"}}, {
                "media/preview.svg": "<svg></svg>",
            })
            assertRejected(customBuild.buildBrowserCatalog)


def testFirmwareRootFallback():
    source = (SOURCE_ROOT / "include" / "webserver.h").read_text(encoding="utf-8")
    assert '#if !HDS_FEATURE_LITTLEFS\nstatic const char HDS_WIFI_SETUP_PAGE' not in source
    assert source.index('server.serveStatic("/", LittleFS, "/")') < source.index('server.on("/", HTTP_GET')
    assert 'request->beginResponse(200, "text/html", HDS_WIFI_SETUP_PAGE)' in source


if __name__ == "__main__":
    testWebapps("--buildfs" in sys.argv)
    testPresentation()
    testFirmwareRootFallback()
    print("webapp contracts passed")
