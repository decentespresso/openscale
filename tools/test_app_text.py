import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "ble-app-text"


def run(command):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout)
        print(result.stderr)
        result.check_returncode()
    return result.stdout.strip()


def main():
    for name in ("include/ble.h", "include/parameter.h", "src/hds.ino", "include/config.h"):
        assert "HDS_ENABLE_APP_PROMPTS" not in (ROOT / name).read_text(encoding="utf-8")
    sketch = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    runtime = (ROOT / "include/app_prompt.h").read_text(encoding="utf-8")
    ble = (ROOT / "include/app_prompt_ble.h").read_text(encoding="utf-8")
    assert "processAppPromptSession();" in sketch
    assert "const bool showPrompt = promptFrame.active && appTextScreenAvailable();" in sketch
    assert "if (appTextScreenAvailable() && cancelAppPrompt())" in sketch
    assert "grinderRuntime.state >= GRINDER_STATE_ARMED" in sketch
    assert "drawWeightAt(f_displayedValue, LCDHeight - 30);" in sketch
    assert "if (!showPrompt) drawButton();" in sketch
    assert "setAppTextEnabled(textEnabled);" in sketch
    assert "saveAppTextEnabled(enabled)" in menu
    assert "storagePutBool(KEY_APP_TEXT, enabled)" in runtime
    assert "App Text o" in menu
    assert "u8g2" not in runtime and "delay(" not in runtime
    assert "appTextEnabled || (length == 1 && bytes[0] == 0)" in ble
    assert "static_cast<uint8_t>(appTextIsEnabled())" in ble
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise RuntimeError("g++ or clang++ is required")
    with tempfile.TemporaryDirectory(prefix="hds-app-text-") as temporary:
        for name in ("state", "runtime", "integration", "grinder_safety"):
            binary = Path(temporary) / (name + (".exe" if os.name == "nt" else ""))
            source = EXAMPLE / "tests" / f"{name}.cpp"
            if name == "grinder_safety":
                template = source.read_text(encoding="utf-8")
                grinder = (ROOT / "include/grinder_runtime.h").read_text(encoding="utf-8")
                for marker, signature in {
                    "CLOSE": "static inline void grinderCloseClient()",
                    "SEND": "static inline bool grinderSendLine(",
                    "ERROR": "static inline void grinderEnterError(",
                    "OK": "static inline void grinderHandleOkResponse(",
                }.items():
                    template = template.replace(f"@{marker}@", block_after(grinder, signature))
                source = Path(temporary) / "grinder_safety.cpp"
                source.write_text(template, encoding="utf-8")
            run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
                 str(source), "-o", str(binary)])
            run([str(binary)])
    print("App Text native state, opt-in, interleaving, and integration selection checks passed")
    print(run(["node", str(EXAMPLE / "tests/browser.mjs")]))


if __name__ == "__main__":
    main()
