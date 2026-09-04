import hashlib
import json
import os
from pathlib import Path
import shutil
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


def writeConfig(
    root, plugins, firmwareRef="v3.1.14", name="custom-build.json", features=None,
):
    path = root / name
    path.write_text(json.dumps({
        "firmware_ref": firmwareRef,
        "features": features or [],
        "plugins": plugins,
    }), encoding="utf-8")
    return path


def writePlugin(
    root, pluginId, patchText=None, conflicts=None, firmwareRefs=None, dependsOn=None,
    recommends=None,
):
    firmwareRefs = firmwareRefs or ["v3.1.14"]
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
        "depends_on": dependsOn or [],
        "conflicts": conflicts or [],
        "recommends": recommends or {"features": [], "plugins": []},
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
    (sourceRoot / "include").mkdir()
    (sourceRoot / "include" / "config.h").write_text(
        '#define HDS_FIRMWARE_VERSION "3.1.14"\n', encoding="utf-8"
    )
    partition = sourceRoot / "partitions" / "test.csv"
    partition.parent.mkdir()
    partition.write_text("# test partition\n", encoding="utf-8")
    tools = sourceRoot / "tools"
    tools.mkdir()
    (tools / "configure_custom_build.py").write_text("source configurator\n", encoding="utf-8")
    (sourceRoot / "git_rev_macro.py").write_text("source version generator\n", encoding="utf-8")
    (sourceRoot / "platformio.ini").write_text(
        "[env:esp32s3]\n"
        "board_build.partitions = partitions/test.csv\n"
        "build_flags = !python3 git_rev_macro.py\n"
        "[env:esp32s3-custom]\n"
        "extends = env:esp32s3\n"
        "extra_scripts = pre:tools/configure_custom_build.py\n",
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
        builderTools = catalogRoot / "tools"
        builderTools.mkdir()
        shutil.copy2(customBuild.SCRIPT_ROOT / "tools" / "configure_custom_build.py", builderTools)
        shutil.copy2(
            customBuild.SCRIPT_ROOT / "tools" / "write_custom_ota_public_key_header.py",
            builderTools,
        )
        builderKeys = catalogRoot / "keys" / "ota"
        builderKeys.mkdir(parents=True)
        for name in customRunner.CUSTOM_OTA_PUBLIC_KEY_NAMES:
            shutil.copy2(customBuild.SCRIPT_ROOT / "keys" / "ota" / name, builderKeys)
        shutil.copy2(customBuild.SCRIPT_ROOT / "git_rev_macro.py", catalogRoot)
        writePlugin(catalogRoot, "asset-only")
        writePlugin(catalogRoot, "dependency")
        writePlugin(
            catalogRoot,
            "patch-a",
            PATCHES["patch-a"],
            dependsOn=["dependency"],
            recommends={"features": ["wifi"], "plugins": ["patch-b"]},
        )
        writePlugin(catalogRoot, "patch-b", PATCHES["patch-b"])
        configPath = writeConfig(catalogRoot, ["patch-b", "asset-only", "patch-a"])
        sourceRoot, sourceCommit = createSourceRepository(root)
        runGit(sourceRoot, "tag", "v1.2.3")
        runGit(sourceRoot, "tag", "v3.1.14")
        with patch.object(customBuild, "ROOT", sourceRoot):
            stableFirmware = customBuild.firmwareMetadata("v1.2.3", sourceRoot)
        assert stableFirmware["custom_version"] == "1.2.3-custom"
        assert stableFirmware["partition_schema"]["path"] == "partitions/test.csv"
        releaseFeatures = {
            featureId: (macro, dependencies, ("v3.1.14",))
            for featureId, (macro, dependencies, _) in customBuild.FEATURES.items()
        }
        with patch.object(customBuild, "ROOT", catalogRoot), patch.object(
            customBuild, "FIRMWARE_REFS", ("v3.1.14",)
        ), patch.object(customBuild, "FEATURES", releaseFeatures), patch.object(
            customRunner, "ROOT", sourceRoot
        ):
            configuration = customBuild.resolveConfiguration(configPath)
            assert [plugin["id"] for plugin in configuration["plugins"]] == [
                "asset-only", "dependency", "patch-a", "patch-b",
            ]
            assert [pluginId for pluginId, _ in configuration["patches"]] == ["patch-a", "patch-b"]
            assertRejected(lambda: customRunner.resolveFirmwareCommit(sourceRoot, "main"))
            assert customRunner.resolveFirmwareCommit(
                sourceRoot, "v3.1.14"
            ) == sourceCommit
            assertRejected(lambda: customRunner.verifySourceCommit(sourceRoot, "main"))

            pluginConfigPath = writeConfig(
                catalogRoot,
                ["patch-a", "patch-b"],
                name="plugin-verify.json",
                features=["wifi"],
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
            customRunner.prepareBuildCheckout(checkoutRoot, catalogRoot)
            reproducibleScript = checkoutRoot / ".pio.nosync" / "reproducible_build.py"
            reproducibleCompiler = reproducibleScript.read_text(encoding="utf-8")
            assert "-ffile-prefix-map=" in reproducibleCompiler
            assert '"-Wl,-s"' in reproducibleCompiler
            filesystemScript = checkoutRoot / ".pio.nosync" / "reproducible_filesystem.py"
            assert "os.utime(path, (epoch, epoch))" in filesystemScript.read_text(encoding="utf-8")
            platformioConfiguration = (checkoutRoot / "platformio.ini").read_text(encoding="utf-8")
            assert "pre:.pio.nosync/reproducible_build.py" in platformioConfiguration
            assert "post:.pio.nosync/reproducible_filesystem.py" in platformioConfiguration
            assert "pre:.pio.nosync/builder-tools/configure_custom_build.py" in platformioConfiguration
            assert "!python3 .pio.nosync/builder-tools/git_rev_macro.py" in platformioConfiguration
            trustedTools = checkoutRoot / ".pio.nosync" / "builder-tools"
            assert (trustedTools / "configure_custom_build.py").read_bytes() == (
                catalogRoot / "tools" / "configure_custom_build.py"
            ).read_bytes()
            assert (trustedTools / "git_rev_macro.py").read_bytes() == (
                catalogRoot / "git_rev_macro.py"
            ).read_bytes()
            assert (trustedTools / "write_custom_ota_public_key_header.py").read_bytes() == (
                catalogRoot / "tools" / "write_custom_ota_public_key_header.py"
            ).read_bytes()
            for name in customRunner.CUSTOM_OTA_PUBLIC_KEY_NAMES:
                assert (trustedTools / name).read_bytes() == (builderKeys / name).read_bytes()
            incompatibleCheckout = root / "incompatible-checkout"
            customRunner.cloneSource(sourceRoot, sourceCommit, incompatibleCheckout)
            incompatiblePlatformio = incompatibleCheckout / "platformio.ini"
            incompatiblePlatformio.write_text(
                incompatiblePlatformio.read_text(encoding="utf-8").replace(
                    "pre:tools/configure_custom_build.py", ""
                ),
                encoding="utf-8",
            )
            assertRejected(
                lambda: customRunner.prepareBuildCheckout(incompatibleCheckout, catalogRoot)
            )
            assert (checkoutRoot / "a.txt").read_text(encoding="utf-8") == "patched a\n"
            assert (checkoutRoot / "b.txt").read_text(encoding="utf-8") == "patched b\n"
            stageRoot = root / "staged-assets"
            customBuild.stageAssets(configuration, stageRoot)
            assert not (stageRoot / "base.html").exists()
            assert (stageRoot / "plugins" / "asset" / "index.html").is_file()
            buildDir = root / "build"
            buildDir.mkdir()
            for name in (
                "firmware.bin", "firmware.factory.bin", "bootloader.bin", "partitions.bin", "littlefs.bin",
            ):
                (buildDir / name).write_bytes(name.encode("ascii"))
            preservedBinaries = root / "preserved-binaries"
            preservedBinaries.mkdir()
            customRunner.copyBuildFiles(
                buildDir, preservedBinaries, customRunner.PROGRAM_BINARIES
            )
            for name in customRunner.PROGRAM_BINARIES:
                (buildDir / name).unlink()
            customRunner.copyBuildFiles(
                preservedBinaries, buildDir, customRunner.PROGRAM_BINARIES
            )
            for name in customRunner.PROGRAM_BINARIES:
                assert (buildDir / name).read_bytes() == name.encode("ascii")
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
            assert first["builder_source"] == sourceCommit
            assert first["firmware_version"] == "3.1.14-custom"
            assert first["combination_input"]["custom_ota_signing_key_generation"] == 1
            assert first["combination_hash"] == customBuild.combinationHash(first["combination_input"])
            assert first["partition_schema"]["path"] == "partitions/test.csv"
            assert list(first["binaries"]) == sorted(customBuild.PUBLIC_BINARIES)
            assert "firmware.factory.bin" not in first["binaries"]
            assert first["archive"] == customBuild.fileMetadata(buildDir / customBuild.FIRMWARE_ARCHIVE)
            patchPackage = next(item for item in first["packages"] if item["id"] == "patch-a")
            assert patchPackage["patches"][0]["sha256"] == hashlib.sha256(
                (
                    catalogRoot / "plugins" / "patch-a" / "patches" /
                    "v3.1.14.patch"
                ).read_bytes()
            ).hexdigest()
            assetPackage = next(item for item in first["packages"] if item["id"] == "asset-only")
            assert assetPackage["assets"][0]["target"] == "plugins/asset/index.html"
            (buildDir / "build-manifest.json").write_bytes(firstManifest.read_bytes())
            signingKey = root / "custom-ota.pem"
            publicKey = root / "custom-ota-public.pem"
            subprocess.run([
                "openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                "-out", str(signingKey),
            ], check=True, capture_output=True)
            subprocess.run([
                "openssl", "pkey", "-in", str(signingKey), "-pubout", "-out", str(publicKey),
            ], check=True, capture_output=True)
            wrongSigningKey = root / "wrong-custom-ota.pem"
            subprocess.run([
                "openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                "-out", str(wrongSigningKey),
            ], check=True, capture_output=True)
            publicKeyFiles = (publicKey, builderKeys / customRunner.CUSTOM_OTA_PUBLIC_KEY_NAMES[1])
            assertRejected(lambda: customRunner.customOta.writeArtifacts(
                buildDir,
                buildDir / "build-manifest.json",
                "https://builds.example.test",
                wrongSigningKey.read_text(encoding="utf-8"),
                publicKeyFiles,
            ))
            assert not (buildDir / "ota-manifest.json").exists()
            assert not (buildDir / "ota-manifest.sig").exists()
            customRunner.customOta.writeArtifacts(
                buildDir,
                buildDir / "build-manifest.json",
                "https://builds.example.test",
                signingKey.read_text(encoding="utf-8"),
                publicKeyFiles,
            )
            subprocess.run([
                "openssl", "dgst", "-sha256", "-verify", str(publicKey),
                "-signature", str(buildDir / "ota-manifest.sig"),
                str(buildDir / "ota-manifest.json"),
            ], check=True, capture_output=True)
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
            changedIdentity = customBuild.combinationInput(
                configuration, sourceCommit, sourceRoot, "9" * 40
            )
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            assetPath = catalogRoot / "plugins" / "asset-only" / "assets" / "index.html"
            assetPath.write_text("changed asset plugin", encoding="utf-8")
            changedIdentity = customBuild.combinationInput(configuration, sourceCommit, sourceRoot)
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            assetPath.write_text("asset plugin", encoding="utf-8")
            patchPath = (
                catalogRoot / "plugins" / "patch-a" / "patches" / "v3.1.14.patch"
            )
            patchPath.write_text(PATCHES["patch-a"].replace("patched a", "changed a"), encoding="utf-8")
            changedIdentity = customBuild.combinationInput(configuration, sourceCommit, sourceRoot)
            assert customBuild.combinationHash(changedIdentity) != combinationHash
            patchPath.write_text(PATCHES["patch-a"], encoding="utf-8")

            malformed = (
                catalogRoot / "plugins" / "patch-b" / "patches" / "v3.1.14.patch"
            )
            malformed.write_text(PATCHES["patch-b"].replace("base b", "missing b"), encoding="utf-8")
            failedCheckout = root / "failed-checkout"
            customRunner.cloneSource(sourceRoot, sourceCommit, failedCheckout)
            assertRejected(lambda: customRunner.applyPatches(configuration, failedCheckout))

            writePlugin(catalogRoot, "patch-a", PATCHES["patch-a"], conflicts=["patch-b"])
            assertRejected(lambda: customBuild.resolveConfiguration(configPath))

            writePlugin(catalogRoot, "wrong-ref", PATCHES["patch-a"], firmwareRefs=["v1.0.0"])
            with patch.object(
                customBuild, "FIRMWARE_REFS", ("v3.1.14", "v1.0.0")
            ):
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
    assert '--builder-commit "${{ inputs.builder_commit }}"' in workflow
    assert '--expected-hash "${{ inputs.combination_hash }}"' in workflow
    assert '--attempt-id "${{ inputs.attempt_id }}"' in workflow
    assert "python tools/update_custom_build_status.py" in workflow
    assert "inputs.combination_hash || github.run_id" in workflow
    assert "detect_plugins:" in workflow
    assert "verify_plugins:" in workflow
    assert "compile_custom:" in workflow
    assert "tools/list_changed_patch_plugins.py" in workflow
    assert "fromJSON(needs.detect_plugins.outputs.matrix)" in workflow
    assert '--verify-plugin-environment "esp32s3-$PLUGIN_ID"' in workflow
    assert "recommendedPluginSelection" in workflow
    verifyPlugins = workflow.split("\n  verify_plugins:\n", 1)[1].split("\n  compile_custom:\n", 1)[0]
    assert "--source-commit" not in verifyPlugins
    assert "verify-pressensor:" not in workflow
    assert "compile-matrix:" not in workflow
    assert "default-web-apps" in workflow
    assert "pio run -e esp32s3-custom" not in workflow
    for path in ('"*.py"', '"keys/**"', '"partitions/**"', '"requirements-platformio.txt"', '"tools/**"'):
        assert path in workflow
    assert "github.workflow_sha == inputs.builder_commit" in workflow
    assert 'ref: ${{ inputs.builder_commit || github.sha }}' in workflow
    configurator = (customBuild.SCRIPT_ROOT / "docs" / "custom-build" / "app.js").read_text(
        encoding="utf-8"
    )
    assert "selectionController = new AbortController()" in configurator
    assert "if (generation !== selectionGeneration) return;" in configurator
    assert "catalogRetryDelay = Math.min(catalogRetryDelay * 2, 30000)" in configurator
    assert "catalogRevisionChanged(fetch, catalog.catalog_revision)" in configurator
    assert "await checkStatus(selection, generation)" in configurator
    assert "navigator.clipboard.writeText(currentCombinationHash)" in configurator
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
    assert "node --test cloudflare/custom-build-worker/test/configurator.test.mjs" in configuratorWorkflow
    print("custom build execution tests passed")


if __name__ == "__main__":
    main()
