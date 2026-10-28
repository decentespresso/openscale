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
    parameters = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    websocket = (ROOT / "include/websocket.h").read_text(encoding="utf-8")
    assert "processAppPromptSession();" in sketch
    assert "const bool showPrompt = promptFrame.active && appTextScreenAvailable();" in sketch
    assert "if (appTextScreenAvailable() && cancelAppPrompt())" in sketch
    assert "grinderRuntime.state >= GRINDER_STATE_ARMED" in sketch
    assert "if (grinderSettings.enabled) grinderRuntimeBegin();" in sketch
    standby = block_after(sketch, "void enterUsbStandby()")
    assert standby.index("stopGrinderForShutdown()") < standby.index("GPIO_power_on_with = BATTERY_CHARGING")
    power = (ROOT / "include/power.h").read_text(encoding="utf-8")
    shutdown = block_after(power, "void shut_down_now_nobeep()")
    assert shutdown.index("stopGrinderForShutdown()") < shutdown.index("esp32_sleep();")
    wifi = (ROOT / "src/wifi_setup.cpp").read_text(encoding="utf-8")
    mdns = block_after(wifi, "if (g_mdnsAdvertisePending)")
    assert mdns.index("if (!g_mdnsReady)") < mdns.index("MDNS.end();")
    assert "drawWeightAt(f_displayedValue, LCDHeight - 30);" in sketch
    assert "if (!showPrompt) drawButton();" in sketch
    assert "setAppTextEnabled(textEnabled);" in sketch
    assert "saveAppTextEnabled(enabled)" in menu
    assert "storagePutBool(KEY_APP_TEXT, enabled)" in runtime
    assert "App Text o" in menu
    assert "u8g2" not in runtime and "delay(" not in runtime
    assert "appTextEnabled || (length == 1 && bytes[0] == 0)" in ble
    assert "static_cast<uint8_t>(appTextIsEnabled())" in ble
    selectPlug = block_after(menu, "uint8_t grinderFindPlugsForSelection()")
    assert selectPlug.index("if (!integrationCanRestart())") < selectPlug.index("grinderReleaseClientForDiscovery();")
    blocked = block_after(menu, "void blockIntegrationChange()")
    assert "grinderRuntime.shutdownUnconfirmed && grinderRuntime.state == GRINDER_STATE_ERROR" in blocked
    assert "grinderRuntimeReset();" in blocked
    assert "grinderSaveSelectedDiscovery" not in blocked
    assert "updateToggleLabel(menuGrinderEnabledLabel, selectedIntegration == IntegrationMode::Grinder);" in menu
    patch = (ROOT / "plugins/pressensor/patches/main.patch").read_text(encoding="utf-8")
    assert "updateToggleLabel(menuPressensorEnabledLabel, selectedIntegration == IntegrationMode::Pressensor);" in patch
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise RuntimeError("g++ or clang++ is required")
    with tempfile.TemporaryDirectory(prefix="hds-app-text-") as temporary:
        for name in ("state", "runtime", "integration", "grinder_safety", "dispatcher"):
            binary = Path(temporary) / (name + (".exe" if os.name == "nt" else ""))
            source = EXAMPLE / "tests" / f"{name}.cpp"
            if name == "integration":
                template = source.read_text(encoding="utf-8")
                replacements = {
                    "MARK": block_after(parameters, "inline void markMenuRestartRequired()"),
                    "LEAVE": block_after(parameters, "inline void leaveMenu()"),
                    "TOGGLE": block_after(menu, "void toggleIntegration(IntegrationMode mode)"),
                    "RESET": block_after(websocket[websocket.rindex("if (mask & WSP_RESET)"):], "if (mask & WSP_RESET)"),
                }
                for marker, contents in replacements.items():
                    template = template.replace(f"@{marker}@", contents)
                source = Path(temporary) / "integration.cpp"
                source.write_text(template, encoding="utf-8")
            if name == "grinder_safety":
                template = source.read_text(encoding="utf-8")
                grinder = (ROOT / "include/grinder_runtime.h").read_text(encoding="utf-8")
                for marker, signature in {
                    "CLOSE": "static inline void grinderCloseClient()",
                    "SEND": "static inline bool grinderSendLine(",
                    "ERROR": "static inline void grinderEnterError(",
                    "OK": "static inline void grinderHandleOkResponse(",
                    "RESOLVE": "static inline void grinderResolveAndConnect()",
                    "STOPPING": "static inline void grinderTickStopping(",
                    "MENU": "static inline void grinderTickWhileMenuOpen(",
                    "PAUSE": "static inline void grinderPauseForMenu()",
                }.items():
                    template = template.replace(f"@{marker}@", block_after(grinder, signature))
                settings = (ROOT / "include/integration_settings.h").read_text(encoding="utf-8")
                template = template.replace("@GUARD@", block_after(settings, "inline bool integrationCanRestart()"))
                template = template.replace("@SHUTDOWN@", block_after(sketch, "bool stopGrinderForShutdown()"))
                source = Path(temporary) / "grinder_safety.cpp"
                source.write_text(template, encoding="utf-8")
            if name == "dispatcher":
                template = source.read_text(encoding="utf-8")
                commands = "\n".join(line for line in parameters.splitlines() if line.startswith("const uint32_t WSP_"))
                template = template.replace("@COMMANDS@", commands)
                for marker, signature in {
                    "QUEUE_OTA": "inline bool remoteQueueWifiUpdate(",
                    "FINISH_OTA": "inline void remoteFinishWifiUpdateDispatch()",
                    "RESTORE": "inline void remoteRestoreDeferredPendingLocked(",
                    "DISPATCH": "void processWsPendingCmds()",
                }.items():
                    template = template.replace(f"@{marker}@", block_after(websocket, signature))
                source = Path(temporary) / "dispatcher.cpp"
                source.write_text(template, encoding="utf-8")
            run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
                 str(source), "-o", str(binary)])
            run([str(binary)])
            if name == "dispatcher":
                run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DHDS_ENABLE_GRINDER=0",
                     str(source), "-o", str(binary)])
                run([str(binary)])
    print("App Text native state, opt-in, interleaving, and integration selection checks passed")
    print(run(["node", str(EXAMPLE / "tests/browser.mjs")]))


if __name__ == "__main__":
    main()
