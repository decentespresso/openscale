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

    stockBuild = firmwareWorkflow.split("\n  build:\n", 1)[1].split("\n  energy-build:\n", 1)[0]
    energyBuild = firmwareWorkflow.split("\n  energy-build:\n", 1)[1]
    assert firmwareWorkflow.count("pio run -e esp32s3\n") == 1
    assert "pio run -e esp32s3-energy-menu" not in stockBuild
    assert energyBuild.count("pio run -e esp32s3-energy-menu") == 1
    assert "pio run -e esp32s3 -t buildfs" in firmwareWorkflow
    assert "esp32s3-grinder" not in firmwareWorkflow
    assert "name: firmware (esp32s3)" in firmwareWorkflow
    assert "name: firmware (esp32s3-energy-menu)" in energyBuild
    assert "for test in tools/test_*.py" in firmwareWorkflow

    assert "detect_plugins:" in customWorkflow
    assert "verify_plugins:" in customWorkflow
    assert "tools/list_changed_patch_plugins.py" in customWorkflow
    assert "fromJSON(needs.detect_plugins.outputs.matrix)" in customWorkflow
    assert '--verify-plugin-environment "esp32s3-$PLUGIN_ID"' in customWorkflow
    assert "recommendedPluginSelection" in customWorkflow
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
        "plugins/default-web-apps/assets/index.html"
    ]) == []
    dependencyCatalog = {
        "base": ({"depends_on": []}, [], {}),
        "dependent": ({"depends_on": ["base"]}, [], {"main": Path("dependent.patch")}),
        "transitive": ({"depends_on": ["dependent"]}, [], {
            "main": Path("transitive.patch"),
            "v1.2.3": Path("transitive-stable.patch"),
        }),
        "unrelated": ({"depends_on": []}, [], {"main": Path("unrelated.patch")}),
    }
    assert changedPlugins.changedPluginMatrix(
        ["plugins/base/plugin.json"], dependencyCatalog
    ) == [
        {"plugin": "dependent", "firmware_ref": "main"},
        {"plugin": "transitive", "firmware_ref": "main"},
        {"plugin": "transitive", "firmware_ref": "v1.2.3"},
    ]
    compileCustom = customWorkflow.split("\n  compile_custom:\n", 1)[1].split("\n  build:\n", 1)[0]
    assert compileCustom.lstrip().startswith("if: github.event_name == 'pull_request'")
    assert '"features": []' in compileCustom
    assert '"plugins": []' in compileCustom
    assert "--config .pio.nosync/pr-ci.json" in compileCustom
    assert '--source-commit "${{ github.sha }}"' in compileCustom
    subprocess.run(
        ["git", "apply", "--check", "--whitespace=error", str(patchPath)],
        cwd=ROOT,
        check=True,
    )

    print("plugin CI contract tests passed")


if __name__ == "__main__":
    main()
