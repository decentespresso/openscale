from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WIFI_OTA_HEADER = ROOT / "include" / "wifi_ota.h"
PULL_OTA_HEADER = ROOT / "include" / "pull_ota.h"
ROLLBACK_HEADER = ROOT / "include" / "ota_rollback.h"
WIFI_SETUP_SOURCE = ROOT / "src" / "wifi_setup.cpp"
HDS_SOURCE = ROOT / "src" / "hds.ino"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
BLE_HEADER = ROOT / "include" / "ble.h"
MENU_HEADER = ROOT / "include" / "menu.h"

WSP_DISPLAY_ON = 1 << 0
WSP_DISPLAY_OFF = 1 << 1
WSP_LOWPWR_ON = 1 << 2
WSP_LOWPWR_OFF = 1 << 3
WSP_SLEEP_ON = 1 << 4
WSP_SLEEP_OFF = 1 << 5
WSP_POWER_OFF = 1 << 6
WSP_TIMER_START = 1 << 7
WSP_TIMER_STOP = 1 << 8
WSP_TIMER_ZERO = 1 << 9
WSP_RESET = 1 << 12
WSP_OTA_RESET = 1 << 14

REPLACEMENT_GROUPS = (
    WSP_DISPLAY_ON | WSP_DISPLAY_OFF,
    WSP_LOWPWR_ON | WSP_LOWPWR_OFF,
    WSP_SLEEP_ON | WSP_SLEEP_OFF,
    WSP_TIMER_START | WSP_TIMER_STOP | WSP_TIMER_ZERO,
)


def assert_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text not in contents:
        raise AssertionError(f"{path.name} missing {text}")


def assert_not_contains(path, text):
    contents = path.read_text(encoding="utf-8")
    if text in contents:
        raise AssertionError(f"{path.name} contains {text}")


def assert_before(path, first, second):
    contents = path.read_text(encoding="utf-8")
    first_at = contents.find(first)
    second_at = contents.find(second)
    if first_at < 0:
        raise AssertionError(f"{path.name} missing {first}")
    if second_at < 0:
        raise AssertionError(f"{path.name} missing {second}")
    if first_at >= second_at:
        raise AssertionError(f"{path.name} must place {first} before {second}")


def assert_pending_sleep_ota_interleaving():
    pending_sleep = True
    soft_sleep = False
    rails_on = True
    ota_active = True

    if ota_active and soft_sleep:
        soft_sleep = False
        rails_on = True
    if pending_sleep:
        pending_sleep = False
        soft_sleep = True
        rails_on = False
    if ota_active and soft_sleep:
        soft_sleep = False
        rails_on = True

    if pending_sleep or soft_sleep or not rails_on:
        raise AssertionError("OTA wake leaves applied soft sleep or powered-off rails")


def assert_ordered(path, snippets):
    contents = path.read_text(encoding="utf-8")
    cursor = 0
    for snippet in snippets:
        index = contents.find(snippet, cursor)
        if index < 0:
            raise AssertionError(f"{path.name} missing ordered snippet: {snippet}")
        cursor = index + len(snippet)


def assert_interleavings():
    pending = WSP_RESET | WSP_TIMER_START
    extracted = pending & WSP_OTA_RESET
    pending &= ~extracted
    if extracted != 0 or pending != WSP_RESET | WSP_TIMER_START:
        raise AssertionError("ordinary remote reset escaped OTA deferral")

    extracted = WSP_DISPLAY_OFF | WSP_TIMER_START | WSP_OTA_RESET
    pending = 0
    deferred = extracted & ~WSP_OTA_RESET
    for group in REPLACEMENT_GROUPS:
        if pending & group:
            deferred &= ~group
    pending |= deferred
    extracted &= WSP_OTA_RESET
    if pending != WSP_DISPLAY_OFF | WSP_TIMER_START or extracted != WSP_OTA_RESET:
        raise AssertionError("OTA start race did not restore extracted remote actions")

    conflicts = (
        (WSP_DISPLAY_OFF, WSP_DISPLAY_ON),
        (WSP_LOWPWR_ON, WSP_LOWPWR_OFF),
        (WSP_SLEEP_ON, WSP_SLEEP_OFF),
        (WSP_TIMER_START, WSP_TIMER_ZERO),
    )
    for older, newer in conflicts:
        deferred = older
        pending = newer
        for group in REPLACEMENT_GROUPS:
            if pending & group:
                deferred &= ~group
        pending |= deferred
        if pending != newer:
            raise AssertionError("restored command overrode newer replacement intent")

    extracted = WSP_POWER_OFF
    pending = 0
    ota_active = True
    if ota_active:
        pending |= extracted & WSP_POWER_OFF
    if pending != WSP_POWER_OFF:
        raise AssertionError("pull OTA start lost an extracted power action")


def main():
    assert_pending_sleep_ota_interleaving()
    assert_interleavings()
    hds = HDS_SOURCE.read_text(encoding="utf-8")
    loop = hds[hds.index("void loop() {"):hds.index("void chargingOLED(")]
    if "if (b_ota || bleHasLiveClient()" not in loop:
        raise AssertionError("OTA does not suspend the auto-sleep countdown")
    ota_wake = 'if (b_ota && b_softSleep) {\n    wakeScaleFromSoftSleep("OTA wake");'
    if ota_wake not in loop:
        raise AssertionError("active OTA does not wake soft-sleep hardware")
    if loop.index("processWsPendingCmds();") > loop.index(ota_wake):
        raise AssertionError("pending sleep must be applied before OTA wake")
    if loop.index(ota_wake) > loop.index("if (!b_softSleep)"):
        raise AssertionError("OTA wake runs after the sleeping-loop gate")

    # ElegantOTA's own auto-reboot bypasses reset()'s mDNS withdrawal; the
    # restart must be queued through the main-loop reset mechanism instead.
    assert_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(false)")
    assert_not_contains(WIFI_OTA_HEADER, "ElegantOTA.setAutoReboot(true)")
    assert_contains(WIFI_OTA_HEADER, "remoteQueueOtaResetAt(millis() + OTA_RESTART_DELAY_MS)")
    assert_not_contains(WIFI_OTA_HEADER, "remoteQueueResetAt(millis() + OTA_RESTART_DELAY_MS)")
    assert_ordered(
        WIFI_OTA_HEADER,
        [
            "void handleElegantOtaStart(AsyncWebServerRequest *request)",
            "std::unique_lock<std::mutex> otaDispatchLock(otaDispatchMutex,",
            "std::try_to_lock",
            "otaDispatchLock.owns_lock()",
            "b_pullOtaRunning || b_ota",
            "onOTAStart();",
            "Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)",
        ],
    )
    assert_ordered(
        WIFI_OTA_HEADER,
        [
            "void onOTAStart()",
            "portENTER_CRITICAL(&wsPendingMux);",
            "b_ota = true;",
            "portEXIT_CRITICAL(&wsPendingMux);",
        ],
    )

    # Pull OTA's success paths must queue the restart rather than calling
    # ESP.restart() directly from the Pull OTA task or the setup() task.
    assert_contains(PULL_OTA_HEADER, "remoteQueueOtaResetAt(millis())")
    pull_ota_contents = PULL_OTA_HEADER.read_text(encoding="utf-8")
    if pull_ota_contents.count("remoteQueueOtaResetAt(millis())") != 2:
        raise AssertionError(
            "pull_ota.h expected exactly two queued restarts "
            "(pullOtaInstall and pullOtaResumePendingLittleFs)"
        )
    assert_not_contains(PULL_OTA_HEADER, "remoteQueueResetAt(millis())")
    assert_not_contains(PULL_OTA_HEADER, "ESP.restart();\n  return true;")

    # hdsOtaRollback() withdraws mDNS/WiFi before the rollback reboot, since
    # the pending-LittleFS flow can have brought WiFi (and mDNS) up first.
    assert_before(
        ROLLBACK_HEADER,
        "stopWifi();",
        "esp_ota_mark_app_invalid_rollback_and_reboot();",
    )

    # setHostname() must run before WiFi.config(), which starts STA and
    # copies the then-current default hostname to the interface.
    assert_before(
        WIFI_SETUP_SOURCE,
        "WiFi.setHostname(params.getMdnsName());",
        "WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);",
    )

    assert_contains(PARAMETER_HEADER, "const uint32_t WSP_OTA_RESET   = 1u << 14;")
    assert_contains(PARAMETER_HEADER, "volatile unsigned long pendingOtaResetAt = 0;")
    assert_contains(PARAMETER_HEADER, "inline void remoteQueueOtaResetAt(unsigned long resetAt)")
    assert_contains(PARAMETER_HEADER, "std::mutex otaDispatchMutex;")
    assert_contains(BLE_HEADER, "remoteQueuePending(WSP_RESET);")
    assert_contains(PARAMETER_HEADER, "volatile bool b_menuRestartRequired = false;")
    assert_contains(
        PARAMETER_HEADER,
        "inline void markMenuRestartRequired() {\n"
        "  portENTER_CRITICAL(&wsPendingMux);\n"
        "  b_menuRestartRequired = true;\n"
        "  portEXIT_CRITICAL(&wsPendingMux);\n"
        "}",
    )
    assert_contains(
        PARAMETER_HEADER,
        "inline void leaveMenu() {\n"
        "  b_menu = false;\n"
        "  portENTER_CRITICAL(&wsPendingMux);\n"
        "  const bool restartRequired = b_menuRestartRequired;\n"
        "  b_menuRestartRequired = false;\n"
        "  portEXIT_CRITICAL(&wsPendingMux);\n"
        "  if (restartRequired) {\n"
        "    remoteQueueResetAt(millis());\n"
        "  }\n"
        "}",
    )
    assert_contains(MENU_HEADER, "void calibrate() {\n  leaveMenu();")
    assert_contains(MENU_HEADER, "pullOtaUpdate(target);\n  leaveMenu();")
    assert_contains(MENU_HEADER, "b_debug = true;\n  leaveMenu();")
    menu_contents = MENU_HEADER.read_text(encoding="utf-8")
    if menu_contents.count("markMenuRestartRequired();") != 2:
        raise AssertionError("menu.h expected restart routing for WiFi toggle and reset")
    for path in [*ROOT.glob("include/*.h"), *ROOT.glob("src/*.ino"), *ROOT.glob("src/*.cpp")]:
        if path != PARAMETER_HEADER:
            assert_not_contains(path, "b_menu = false;")

    websocket = WEBSOCKET_HEADER.read_text(encoding="utf-8")
    dispatcher_start = websocket.index("void processWsPendingCmds() {")
    dispatcher = websocket[dispatcher_start:websocket.index("#if HDS_FEATURE_WEBSOCKET", dispatcher_start)]
    assert_ordered(
        WEBSOCKET_HEADER,
        [
            "uint32_t mask = b_ota ? (wsPendingMask & WSP_OTA_RESET) : wsPendingMask;",
            "wsPendingMask &= ~mask;",
            "if (mask & WSP_OTA_RESET)",
        ],
    )
    dispatch_lock = "std::lock_guard<std::mutex> otaDispatchLock(otaDispatchMutex);"
    race_check = dispatcher.index("if (b_ota) {")
    if dispatcher.index(dispatch_lock) > race_check:
        raise AssertionError("OTA lifecycle lock starts after the OTA recheck")
    pre_race = dispatcher[dispatcher.index("portEXIT_CRITICAL(&wsPendingMux);"):race_check]
    for action in ["reset();", "u8g2.", "wakeScaleFromSoftSleep", "stopWatch.", "wifiUpdate();", "b_powerOff = true;"]:
        if action in pre_race:
            raise AssertionError(f"pending action dispatches before OTA recheck: {action}")
    if "mask & ~(WSP_OTA_RESET | WSP_WIFI_UPDATE)" not in dispatcher:
        raise AssertionError("OTA start race does not restore extracted remote actions")
    restore_start = websocket.index("inline void remoteRestoreDeferredPendingLocked(")
    restore = websocket[restore_start:websocket.index("inline void wsQueuePending(", restore_start)]
    for group in [
        "WSP_DISPLAY_ON | WSP_DISPLAY_OFF",
        "WSP_LOWPWR_ON | WSP_LOWPWR_OFF",
        "WSP_SLEEP_ON | WSP_SLEEP_OFF",
        "WSP_TIMER_START | WSP_TIMER_STOP | WSP_TIMER_ZERO",
    ]:
        if group not in restore:
            raise AssertionError(f"restoration does not preserve newer replacement group: {group}")
    wifi_dispatch = dispatcher.index("remoteRestoreDeferredPendingLocked(mask & ~WSP_WIFI_UPDATE")
    if wifi_dispatch > dispatcher.index("if (mask & WSP_DISPLAY_ON)"):
        raise AssertionError("WiFi OTA dispatch does not defer hardware actions")
    wifi_tail = dispatcher[wifi_dispatch:dispatcher.index("#if HDS_ENABLE_ENERGY_MENU", wifi_dispatch)]
    if wifi_tail.index("wifiUpdate(otaTarget);") > wifi_tail.index("remoteFinishWifiUpdateDispatch();"):
        raise AssertionError("WiFi OTA dispatch is released before the update starts")
    if wifi_tail.index("remoteFinishWifiUpdateDispatch();") > wifi_tail.index("return;"):
        raise AssertionError("WiFi OTA dispatch continues into deferred hardware actions")

    print("OTA reboot routing contract tests passed")


if __name__ == "__main__":
    main()
