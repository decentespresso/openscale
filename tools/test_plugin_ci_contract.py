import json
from pathlib import Path
import subprocess

import list_changed_patch_plugins as changedPlugins


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def main():
    firmwareWorkflow = read(".github/workflows/nightly.yml")
    customWorkflow = read(".github/workflows/custom-build.yml")
    otaWorkflow = read(".github/workflows/ota-contracts.yml")
    platformio = read("platformio.ini")
    patchPath = ROOT / "plugins" / "pressensor" / "patches" / "main.patch"
    patch = patchPath.read_text(encoding="utf-8")
    manifest = json.loads(read("plugins/pressensor/plugin.json"))

    assert firmwareWorkflow.count("pio run -e esp32s3\n") == 1
    assert "pio run -e esp32s3 -t buildfs" in firmwareWorkflow
    assert "esp32s3-grinder" not in firmwareWorkflow
    assert "name: firmware (esp32s3)" in firmwareWorkflow
    assert "python tools/test_plugin_ci_contract.py" in firmwareWorkflow

    assert "detect_plugins:" in customWorkflow
    assert "verify_plugins:" in customWorkflow
    assert "tools/list_changed_patch_plugins.py" in customWorkflow
    assert "fromJSON(needs.detect_plugins.outputs.matrix)" in customWorkflow
    assert '--verify-plugin-environment "esp32s3-$PLUGIN_ID"' in customWorkflow
    assert '--source-commit "${{ github.sha }}"' in customWorkflow
    assert "verify-pressensor:" not in customWorkflow
    assert "compile-matrix:" not in customWorkflow
    dispatchBuild = customWorkflow.split("\n  build:\n", 1)[1]
    assert dispatchBuild.lstrip().startswith("if: github.event_name == 'workflow_dispatch'")

    assert "dependency-build:" not in otaWorkflow
    assert "pio run -e" not in otaWorkflow

    assert "[env:esp32s3-grinder]" in platformio
    assert "[env:esp32s3-pressensor]" in patch
    assert "#ifdef HDS_CUSTOM_BUILD" in patch
    assert manifest["patches"] == {"main": "patches/main.patch"}
    assert changedPlugins.changedPluginIds([
        "plugins/pressensor/plugin.json",
        "plugins/pressensor/patches/main.patch",
        "docs/plugin-development.md",
    ]) == ["pressensor"]
    assert changedPlugins.changedPluginMatrix([
        "plugins/pressensor/patches/main.patch"
    ]) == [{"plugin": "pressensor", "firmware_ref": "main"}]
    assert changedPlugins.changedPluginMatrix([
        "plugins/hello-web/hello.html"
    ]) == []
    subprocess.run(
        ["git", "apply", "--check", "--whitespace=error", str(patchPath)],
        cwd=ROOT,
        check=True,
    )

    print("plugin CI contract tests passed")


if __name__ == "__main__":
    main()
