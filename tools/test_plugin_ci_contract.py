import json
from pathlib import Path
import subprocess


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

    assert "verify-pressensor:" in customWorkflow
    assert "name: plugin (esp32s3-pressensor)" in customWorkflow
    assert customWorkflow.count("--verify-plugin-environment esp32s3-pressensor") == 1
    assert "compile-matrix:" not in customWorkflow
    dispatchBuild = customWorkflow.split("\n  build:\n", 1)[1]
    assert dispatchBuild.lstrip().startswith("if: github.event_name == 'workflow_dispatch'")

    assert "dependency-build:" not in otaWorkflow
    assert "pio run -e" not in otaWorkflow

    assert "[env:esp32s3-grinder]" in platformio
    assert "[env:esp32s3-pressensor]" in patch
    assert "#ifdef HDS_CUSTOM_BUILD" in patch
    assert manifest["patches"] == {"main": "patches/main.patch"}
    subprocess.run(
        ["git", "apply", "--check", "--whitespace=error", str(patchPath)],
        cwd=ROOT,
        check=True,
    )

    print("plugin CI contract tests passed")


if __name__ == "__main__":
    main()
