import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

import configure_custom_build as customBuild
from build_custom_firmware import applyPatches, resolveFirmwareCommit


ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "plugins" / "app-prompts"


def run(command, cwd=ROOT):
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True)


def testFirmware():
    configuration = customBuild.resolveConfiguration(PLUGIN / "examples" / "custom-build.json")
    assert configuration["features"] == []
    assert [plugin["id"] for plugin in configuration["plugins"]] == ["app-prompts"]
    assert not configuration["assets"]
    try:
        revision = resolveFirmwareCommit(ROOT, "main")
    except ValueError:
        revision = run(["git", "rev-parse", "HEAD"]).stdout.decode("ascii").strip()
    archive = run(["git", "archive", "--format=zip", revision,
                   "include/config.h", "include/parameter.h", "include/ble.h", "src/hds.ino", "platformio.ini"]).stdout
    with tempfile.TemporaryDirectory(prefix="hds-app-prompts-") as temporary:
        checkout = Path(temporary)
        with zipfile.ZipFile(io.BytesIO(archive)) as source:
            source.extractall(checkout)
        applyPatches(configuration, checkout)
        runtime = (checkout / "include" / "app_prompt.h").read_text(encoding="utf-8")
        state = (checkout / "include" / "app_prompt_state.h").read_text(encoding="utf-8")
        parameter = (checkout / "include" / "parameter.h").read_text(encoding="utf-8")
        ble = (checkout / "include" / "ble.h").read_text(encoding="utf-8")
        sketch = (checkout / "src" / "hds.ino").read_text(encoding="utf-8")
        config = (checkout / "include" / "config.h").read_text(encoding="utf-8")
        platformio = (checkout / "platformio.ini").read_text(encoding="utf-8")
        assert "#if HDS_CUSTOM_BUILD\n#define HDS_ENABLE_APP_PROMPTS 1" in config
        assert "#define HDS_ENABLE_APP_PROMPTS 0" in config
        assert "[env:esp32s3-app-prompts]" in platformio
        assert "-DHDS_ENABLE_APP_PROMPTS=1" in platformio
        for text in (parameter, ble, sketch):
            assert "#if HDS_ENABLE_APP_PROMPTS" in text
        assert "volatile AppPromptState appPromptState;" in parameter
        assert "portMUX_TYPE appPromptMux" in parameter
        assert "portENTER_CRITICAL(&appPromptMux)" in runtime
        assert "u8g2" not in runtime and "delay(" not in runtime
        assert "8b8dc7c0-6bf7-4bdf-9a2c-0ba9cb9a4a01" in runtime
        assert "String" not in state
        assert "recordAppPromptHeartbeat(data, len);" in ble
        assert "syncAppPromptConnection(desc->conn_handle);" in ble
        assert "setupAppPromptCharacteristic(pService);" in ble
        assert "processAppPromptSession();" in sketch
        assert "circle_press_data = {};" in sketch
        assert "if (!showPrompt) drawButton();" in sketch
        assert "mixEnergyDisplayText(signature, promptFrame.text)" in sketch
        assert "uint32_t signature = energyDisplaySignature(frameNow);" in sketch
        assert "snapshotAppPrompt().active" in sketch
        assert "f_displayedValue <= OVER_WEIGHT" in sketch
        assert "drawWeightAt(f_displayedValue, LCDHeight - 30);" in sketch
        assert "u8g2.getStrWidth(promptFrame.text)) / 2, LCDHeight - 16, promptFrame.text);" in sketch
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            raise RuntimeError("g++ or clang++ is required for the App Prompts state check")
        for name in ("state", "runtime"):
            executable = checkout / (f"{name}.exe" if os.name == "nt" else name)
            run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-I", str(checkout / "include"), str(PLUGIN / "tests" / f"{name}.cpp"),
                 "-o", str(executable)])
            run([str(executable)])
    print(f"App Prompts firmware checks passed against {revision}")


def main():
    try:
        testFirmware()
        result = run(["node", str(PLUGIN / "tests" / "browser.mjs")])
        print(result.stdout.decode("utf-8").strip())
        with tempfile.TemporaryDirectory(prefix="hds-app-prompts-site-") as temporary:
            checkout = Path(temporary)
            script = checkout / "cocktails" / "js" / "scale_maker.js"
            script.parent.mkdir(parents=True)
            shutil.copyfile(PLUGIN / "tests" / "fixtures" / "scale_maker.js", script)
            run(["git", "apply", "--check", "--whitespace=error",
                 str(PLUGIN / "examples" / "cocktail-maker.patch")], cwd=checkout)
            run(["git", "apply", str(PLUGIN / "examples" / "cocktail-maker.patch")], cwd=checkout)
            result = run(["node", "--experimental-vm-modules",
                          str(PLUGIN / "tests" / "cocktail-site.mjs"), str(script)])
            print(result.stdout.decode("utf-8").strip())
    except subprocess.CalledProcessError as error:
        print(error.stdout.decode("utf-8", errors="replace"))
        print(error.stderr.decode("utf-8", errors="replace"))
        raise


if __name__ == "__main__":
    main()
