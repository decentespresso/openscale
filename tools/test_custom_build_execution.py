import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
from unittest.mock import patch

import build_custom_firmware as customRunner
import configure_custom_build as customBuild


PATCHES = {
    "patch-a": """diff --git a/a.txt b/a.txt
--- a/a.txt
+++ b/a.txt
@@ -1 +1 @@
-base a
+patched a
""",
    "patch-b": """diff --git a/b.txt b/b.txt
--- a/b.txt
+++ b/b.txt
@@ -1 +1 @@
-base b
+patched b
""",
}


def runGit(root, *arguments):
    return subprocess.run(
        ["git", *arguments],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def writeConfig(root, plugins, firmwareRef="main", name="custom-build.json"):
    path = root / name
    path.write_text(json.dumps({
        "firmware_ref": firmwareRef,
        "features": [],
        "plugins": plugins,
    }), encoding="utf-8")
    return path


def writePlugin(root, pluginId, patchText=None, conflicts=None, firmwareRefs=None):
    firmwareRefs = firmwareRefs or ["main"]
    pluginDir = root / "plugins" / pluginId
    pluginDir.mkdir(parents=True, exist_ok=True)
    patches = {}
    if patchText is not None:
        patchPath = pluginDir / "patches" / f"{firmwareRefs[0]}.patch"
        patchPath.parent.mkdir(parents=True, exist_ok=True)
        patchPath.write_text(patchText, encoding="utf-8")
        patches[firmwareRefs[0]] = f"patches/{firmwareRefs[0]}.patch"
    assets = []
    if pluginId == "asset-only":
        assetPath = pluginDir / "assets" / "index.html"
        assetPath.parent.mkdir(parents=True, exist_ok=True)
        assetPath.write_text("asset plugin", encoding="utf-8")
        assets.append({"source": "assets/index.html", "target": "plugins/asset/index.html"})
    manifest = {
        "schema": 2,
        "id": pluginId,
        "name": pluginId,
        "description": "Phase 2 test plugin.",
        "tooltip": "Phase 2 test plugin.",
        "version": "1.0.0",
        "firmware_refs": firmwareRefs,
        "requires": ["littlefs"] if assets else [],
        "conflicts": conflicts or [],
        "patches": patches,
        "assets": assets,
        "budget": {
            "firmware_flash_bytes": 0,
            "static_ram_bytes": 0,
            "littlefs_bytes": 1024 if assets else 0,
        },
    }
    (pluginDir / "plugin.json").write_text(json.dumps(manifest), encoding="utf-8")


def createSourceRepository(root):
    sourceRoot = root / "source-repository"
    sourceRoot.mkdir()
    runGit(sourceRoot, "init", "-b", "main")
    runGit(sourceRoot, "config", "user.email", "custom-build@example.invalid")
    runGit(sourceRoot, "config", "user.name", "Custom Build Test")
    (sourceRoot / "a.txt").write_text("base a\n", encoding="utf-8")
    (sourceRoot / "b.txt").write_text("base b\n", encoding="utf-8")
    (sourceRoot / "web_apps").mkdir()
    (sourceRoot / "web_apps" / "base.html").write_text("base asset", encoding="utf-8")
    partition = sourceRoot / "partitions" / "test.csv"
    partition.parent.mkdir()
    partition.write_text("# test partition\n", encoding="utf-8")
    (sourceRoot / "platformio.ini").write_text(
        "[env:esp32s3]\nboard_build.partitions = partitions/test.csv\n"
        "[env:esp32s3-custom]\nextends = env:esp32s3\n",
        encoding="utf-8",
    )
    runGit(sourceRoot, "add", ".")
    runGit(sourceRoot, "commit", "-m", "test source")
    return sourceRoot, runGit(sourceRoot, "rev-parse", "HEAD")


def assertRejected(action, exceptionType=ValueError):
    try:
        action()
    except exceptionType:
        return
    raise AssertionError("invalid custom build operation accepted")


def main():
    assertRejected(lambda: customBuild.safeRelativePath("plugins/naive-\u00e9.html", "test path"))
    with patch.dict(os.environ, {}, clear=True):
        assert customRunner.pullRequestCommit() is None
    with patch.dict(os.environ, {"GITHUB_EVENT_NAME": "pull_request"}, clear=True):
        assertRejected(customRunner.pullRequestCommit)
    with patch.dict(
        os.environ,
        {"GITHUB_EVENT_NAME": "pull_request", "GITHUB_SHA": "1" * 40},
        clear=True,
    ):
        assert customRunner.pullRequestCommit() == "1" * 40
        assert customRunner.requestedSourceCommit(None, None) == "1" * 40
        assert customRunner.requestedSourceCommit(None, "esp32s3-pressensor") is None
        assert customRunner.requestedSourceCommit("2" * 40, "esp32s3-pressensor") == "2" * 40
    with tempfile.TemporaryDirectory() as temporaryDirectory:
        root = Path(temporaryDirectory)
        catalogRoot = root / "catalog"
        catalogRoot.mkdir()
        (catalogRoot / "web_apps").mkdir()
        writePlugin(catalogRoot, "asset-only")
        writePlugin(catalogRoot, "patch-a", PATCHES["patch-a"])
        writePlugin(catalogRoot, "patch-b", PATCHES["patch-b"])
        configPath = writeConfig(catalogRoot, ["patch-b", "asset-only", "patch-a"])
        sourceRoot, sourceCommit = createSourceRepository(root)
        with patch.object(customBuild, "ROOT", catalogRoot), patch.object(customRunner, "ROOT", sourceRoot):
            configuration = customBuild.resolveConfiguration(configPath)
            assert [plugin["id"] for plugin in configuration["plugins"]] == [
                "asset-only", "patch-a", "patch-b",
            ]
            assert [pluginId for pluginId, _ in configuration["patches"]] == ["patch-a", "patch-b"]
            assert customRunner.resolveFirmwareCommit(sourceRoot, "main") == sourceCommit
            assertRejected(lambda: customRunner.verifySourceCommit(sourceRoot, "main"))

            pluginConfigPath = writeConfig(
                catalogRoot, ["patch-a"], name="plugin-verify.json"
            )

            def createPluginCheckout(repositoryRoot, commitSha, checkoutRoot):
                checkoutRoot.mkdir()
                tools = checkoutRoot / "tools"
                tools.mkdir()
                (tools / "test_patch_a_contract.py").write_text("", encoding="utf-8")

            with (
                patch.object(customRunner, "verifySourceCommit", return_value=sourceCommit),
                patch.object(customRunner, "cloneSource", side_effect=createPluginCheckout),
                patch.object(customRunner, "applyPatches") as applyPatches,
                patch.object(customRunner, "runCommand") as runCommand,
            ):
                verification = customRunner.verifyPluginEnvironment(
                    pluginConfigPath, "esp32s3-patch-a", sourceCommit
                )
            assert verification == {
                "base_source": sourceCommit,
                "environment": "esp32s3-patch-a",
                "plugin": "patch-a",
                "tests": ["test_patch_a_contract.py"],
            }
            applyPatches.assert_called_once()
            commands = [call.args[0] for call in runCommand.call_args_list]
            assert commands[0][0] == customRunner.sys.executable
            assert commands[0][1].endswith("test_patch_a_contract.py")
            assert commands[1] == ["pio", "run", "-e", "esp32s3-patch-a"]
            assertRejected(lambda: customRunner.verifyPluginEnvironment(
                pluginConfigPath, "esp32s3-other", sourceCommit
            ))
            assertRejected(lambda: customRunner.verifyPluginEnvironment(
                configPath, "esp32s3-patch-a", sourceCommit
            ))

            checkoutRoot = root / "valid-checkout"
            customRunner.cloneSource(sourceRoot, sourceCommit, checkoutRoot)
            assert customRunner.sourceDateEpoch(checkoutRoot) == runGit(sourceRoot, "show", "-s", "--format=%ct", "HEAD")
            customRunner.applyPatches(configuration, checkoutRoot)
            customRunner.configureReproducibleBuild(checkoutRoot)
            reproducibleScript = checkoutRoot / ".pio.nosync" / "reproducible_build.py"
            reproducibleCompiler = reproducibleScript.read_text(encoding="utf-8")
            assert "-ffile-prefix-map=" in reproducibleCompiler
            assert '"-Wl,-s"' in reproducibleCompiler
            filesystemScript = checkoutRoot / ".pio.nosync" / "reproducible_filesystem.py"
            assert "os.utime(path, (epoch, epoch))" in filesystemScript.read_text(encoding="utf-8")
            platformioConfiguration = (checkoutRoot / "platformio.ini").read_text(encoding="utf-8")
            assert "pre:.pio.nosync/reproducible_build.py" in platformioConfiguration
            assert "post:.pio.nosync/reproducible_filesystem.py" in platformioConfiguration
            assert (checkoutRoot / "a.txt").read_text(encoding="utf-8") == "patched a\n"
            assert (checkoutRoot / "b.txt").read_text(encoding="utf-8") == "patched b\n"
            stageRoot = root / "staged-assets"
            customBuild.stageAssets(configuration, stageRoot, sourceRoot)
            assert (stageRoot / "base.html").is_file()
            assert (stageRoot / "plugins" / "asset" / "index.html").is_file()
            buildDir = root / "build"
            buildDir.mkdir()
            for name in (
                "firmware.bin", "firmware.factory.bin", "bootloader.bin", "partitions.bin", "littlefs.bin",
            ):
                (buildDir / name).write_bytes(name.encode("ascii"))
            (buildDir / "dependencies.txt").write_text("PlatformIO Core", encoding="utf-8")
            customRunner.createFirmwareArchive(buildDir)
            archiveBytes = (buildDir / customBuild.FIRMWARE_ARCHIVE).read_bytes()
            customRunner.createFirmwareArchive(buildDir)
            assert (buildDir / customBuild.FIRMWARE_ARCHIVE).read_bytes() == archiveBytes
            with zipfile.ZipFile(buildDir / customBuild.FIRMWARE_ARCHIVE) as archive:
                assert archive.namelist() == list(customBuild.PUBLIC_BINARIES)
            firstManifest = buildDir / "first.json"
            secondManifest = buildDir / "second.json"
            customBuild.writeBuildManifest(
                configuration, buildDir, firstManifest, sourceCommit, sourceRoot
            )
            customBuild.writeBuildManifest(
                configuration, buildDir, secondManifest, sourceCommit, sourceRoot
            )
            first = json.loads(firstManifest.read_text(encoding="utf-8"))
            second = json.loads(secondManifest.read_text(encoding="utf-8"))
            assert first == second
            assert first["base_source"] == sourceCommit
            assert first["combination_hash"] == customBuild.combinationHash(first["combination_input"])
            assert first["partition_schema"]["path"] == "partitions/test.csv"
            assert list(first["binaries"]) == sorted(customBuild.PUBLIC_BINARIES)
            assert "firmware.factory.bin" not in first["binaries"]
            assert first["archive"] == customBuild.fileMetadata(buildDir / customBuild.FIRMWARE_ARCHIVE)
            patchPackage = next(item for item in first["packages"] if item["id"] == "patch-a")
            assert patchPackage["patches"][0]["sha256"] == hashlib.sha256(
                (catalogRoot / "plugins" / "patch-a" / "patches" / "main.patch").read_bytes()
            ).hexdigest()
            assetPackage = next(item for item in first["packages"] if item["id"] == "asset-only")
            assert assetPackage["assets"][0]["target"] == "plugins/asset/index.html"
            (buildDir / "build-manifest.json").write_bytes(firstManifest.read_bytes())
            customRunner.requireBuildFiles(buildDir)
            combinationHash = first["combination_hash"]
            combinationInput = first["combination_input"]
            cacheRoot = root / "published"
            outputDir = cacheRoot / combinationHash
            customRunner.publishArtifacts(buildDir, outputDir)
            assert sorted(path.name for path in outputDir.iterdir()) == sorted(customRunner.BUILD_FILES)
            assert customRunner.completeCacheEntry(outputDir, combinationHash, combinationInput)
            assertRejected(lambda: customRunner.publishArtifacts(buildDir, outputDir))
            dependencies = outputDir / "dependencies.txt"
            dependencies.write_text("incomplete", encoding="utf-8")
            assert not customRunner.completeCacheEntry(outputDir, combinationHash, combinationInput)
            dependencies.write_text("PlatformIO Core", encoding="utf-8")
            with patch.object(customRunner, "applyPatches") as applyPatches:
                result = customRunner.buildCustomFirmware(
                    configPath, cacheRoot, sourceCommit, expectedHash=combinationHash
                )
            assert result["cache_hit"] is True
            assert result["combination_hash"] == combinationHash
            applyPatches.assert_not_called()
            assertRejected(lambda: customRunner.buildCustomFirmware(
                configPath, cacheRoot, sourceCommit, expectedHash="0" * 64
            ))
            githubOutput = root / "github-output.txt"
            customRunner.writeGithubOutput(githubOutput, result)
            assert githubOutput.read_text(encoding="utf-8").splitlines() == [
                f"combination_hash={combinationHash}",
                f"output={outputDir}",
                "cache_hit=true",
            ]

            changedIdentity = customBuild.combinationInput(configuration, "0" * 40, sourceRoot)
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            assetPath = catalogRoot / "plugins" / "asset-only" / "assets" / "index.html"
            assetPath.write_text("changed asset plugin", encoding="utf-8")
            changedIdentity = customBuild.combinationInput(configuration, sourceCommit, sourceRoot)
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            assetPath.write_text("asset plugin", encoding="utf-8")
            patchPath = catalogRoot / "plugins" / "patch-a" / "patches" / "main.patch"
            patchPath.write_text(PATCHES["patch-a"].replace("patched a", "changed a"), encoding="utf-8")
            changedIdentity = customBuild.combinationInput(configuration, sourceCommit, sourceRoot)
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            patchPath.write_text(PATCHES["patch-a"], encoding="utf-8")

            malformed = catalogRoot / "plugins" / "patch-b" / "patches" / "main.patch"
            malformed.write_text(PATCHES["patch-b"].replace("base b", "missing b"), encoding="utf-8")
            failedCheckout = root / "failed-checkout"
            customRunner.cloneSource(sourceRoot, sourceCommit, failedCheckout)
            assertRejected(lambda: customRunner.applyPatches(configuration, failedCheckout))

            writePlugin(catalogRoot, "patch-a", PATCHES["patch-a"], conflicts=["patch-b"])
            assertRejected(lambda: customBuild.resolveConfiguration(configPath))

            writePlugin(catalogRoot, "wrong-ref", PATCHES["patch-a"], firmwareRefs=["v1.0.0"])
            with patch.object(customBuild, "FIRMWARE_REFS", ("main", "v1.0.0")):
                assertRejected(lambda: customBuild.resolveConfiguration(
                    writeConfig(catalogRoot, ["wrong-ref"])
                ))

        assert runGit(sourceRoot, "status", "--porcelain") == ""

    workflow = (customBuild.SCRIPT_ROOT / ".github" / "workflows" / "custom-build.yml").read_text(
        encoding="utf-8"
    )
    assert "fetch-depth: 0" in workflow
    assert "python tools/test_custom_build_execution.py" in workflow
    assert "python tools/build_custom_firmware.py" in workflow
    assert "--cache-url https://openscale-custom-builds.odevstudio.workers.dev" in workflow
    assert "python tools/publish_custom_build.py" in workflow
    assert "steps.custom_build.outputs.combination_hash" in workflow
    assert '--source-commit "${{ inputs.source_commit }}"' in workflow
    assert '--expected-hash "${{ inputs.combination_hash }}"' in workflow
    assert '--attempt-id "${{ inputs.attempt_id }}"' in workflow
    assert "python tools/update_custom_build_status.py" in workflow
    assert "inputs.combination_hash || github.run_id" in workflow
    assert "verify-pressensor:" in workflow
    assert "--verify-plugin-environment esp32s3-pressensor" in workflow
    assert '"plugins": ["pressensor"]' in workflow
    assert "compile-matrix:" not in workflow
    assert "hello-web" in workflow
    assert "pio run -e esp32s3-custom" not in workflow
    configurator = (customBuild.SCRIPT_ROOT / "docs" / "custom-build" / "app.js").read_text(
        encoding="utf-8"
    )
    assert "selectionController = new AbortController()" in configurator
    assert "if (generation !== selectionGeneration) return;" in configurator
    assert "currentCombinationHash" not in configurator
    assert "expectedHash" not in configurator
    configuratorWorkflow = (
        customBuild.SCRIPT_ROOT / ".github" / "workflows" / "custom-build-configurator.yml"
    ).read_text(encoding="utf-8")
    assert '".github/workflows/custom-build-configurator.yml"' in configuratorWorkflow
    assert '"docs/custom-build/**"' in configuratorWorkflow
    assert '"!docs/custom-build/catalog.json"' in configuratorWorkflow
    assert '"!docs/custom-build/service-catalog.json"' in configuratorWorkflow
    assert "python tools/test_plugin_catalog.py" in configuratorWorkflow
    assert "python tools/test_custom_build_execution.py" in configuratorWorkflow
    assert "node --check docs/custom-build/app.js" in configuratorWorkflow
    print("custom build execution tests passed")


if __name__ == "__main__":
    main()
