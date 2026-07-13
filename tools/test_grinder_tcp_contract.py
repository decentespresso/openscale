import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_HEADER = ROOT / "include" / "grinder_protocol.h"
RUNTIME_HEADER = ROOT / "include" / "grinder_runtime.h"
LOW_LATENCY_HEADER = ROOT / "include" / "grinder_runtime_low_latency.h"
DISCOVERY_HEADER = ROOT / "include" / "grinder_discovery.h"
ADAPTIVE_HEADER = ROOT / "include" / "grinder_adaptive_safety.h"
RUNTIME_ADAPTIVE_HEADER = ROOT / "include" / "grinder_runtime_adaptive.h"
PERSISTENCE_HEADER = ROOT / "include" / "grinder_settings_persistence.h"
MENU_HEADER = ROOT / "include" / "menu.h"
HDS_SOURCE = ROOT / "src" / "hds.ino"
POWER_HEADER = ROOT / "include" / "power.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
HOST_CONTRACT = ROOT / "tools" / "test_grinder_contract.cpp"


def source(path):
    return path.read_text(encoding="utf-8")


def assert_contains(path, text):
    assert text in source(path), f"{path} missing {text!r}"


def assert_not_contains(path, text):
    assert text not in source(path), f"{path} unexpectedly contains {text!r}"


def test_production_contract():
    assert HOST_CONTRACT.exists()
    compiler = next((shutil.which(name) for name in ("g++", "clang++") if shutil.which(name)), None)
    if compiler is None:
        print("native C++ grinder contract skipped: no host compiler")
        return
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "grinder_contract"
        subprocess.run(
            [compiler, "-std=c++11", "-I", str(ROOT / "include"), str(HOST_CONTRACT), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def test_wrong_mac_runtime_contract():
    assert_contains(RUNTIME_HEADER, "if (!grinderResponseMatchesSelection(response))")
    assert_contains(RUNTIME_HEADER, 'grinderEnterError("wrong mac")')


def test_adaptive_safety_source_contracts():
    adaptive = source(ADAPTIVE_HEADER)
    runtime = source(RUNTIME_HEADER)
    runtime_adaptive = source(RUNTIME_ADAPTIVE_HEADER)
    assert "GRINDER_ADAPTIVE_MAX_AVERAGE_RATE_GPS 6.0f" in adaptive
    assert "GRINDER_ADAPTIVE_FINAL_DELAY_MS 1000" in adaptive
    assert "GRINDER_ADAPTIVE_FINAL_STABLE_MS 1500" in adaptive
    assert "GRINDER_ADAPTIVE_FINAL_STABLE_TOLERANCE_GRAMS 0.15f" in adaptive
    assert "GRINDER_ADAPTIVE_STALL_MS 2000" in adaptive
    assert "GRINDER_ADAPTIVE_MAX_POST_OFF_DROP_GRAMS 0.50f" in adaptive
    assert "weight + GRINDER_ADAPTIVE_MAX_POST_OFF_DROP_GRAMS < shot->finalWeight" in adaptive
    assert "now - shot->offAt >= GRINDER_ADAPTIVE_FINAL_DELAY_MS && now - shot->stableSinceAt >= GRINDER_ADAPTIVE_FINAL_STABLE_MS" in adaptive
    assert "shot->finalWeight < targetGrams && now - shot->lastIncreaseAt >= GRINDER_ADAPTIVE_STALL_MS" in adaptive
    assert "grinderAdaptiveSafetyCalculate" in adaptive
    assert "grinderAdaptiveSafetyRecord" in adaptive
    assert "grinderAdaptiveShotInvalidate" in adaptive
    assert "grinderAdaptiveShotMarkOff" in adaptive
    assert "GRINDER_ADAPTIVE_SKIP_POST_OFF_DROP" in adaptive
    assert "GRINDER_ADAPTIVE_SKIP_REMOVED" in adaptive
    assert "removed && !grinderRuntime.adaptiveShot.finalWeightLocked" in runtime
    assert "grinderInvalidateAdaptiveShot(GRINDER_ADAPTIVE_SKIP_REMOVED)" in runtime
    assert "grinderLearnAdaptiveSafetyIfReady();" in runtime
    assert "grinderLearnAdaptiveSafety();" in runtime
    assert "grinderMarkSettingsDirty();" in runtime_adaptive
    assert "grinderSaveSettings();" not in runtime_adaptive


def test_low_latency_cutoff_source_contracts():
    low_latency = source(LOW_LATENCY_HEADER)
    runtime = source(RUNTIME_HEADER)
    tare_requested_start = low_latency.index("static inline void grinderRuntimeNotifyTareRequested")
    tare_complete_start = low_latency.index("static inline void grinderRuntimeNotifyTareComplete")
    tare_complete_end = low_latency.index("static inline void grinderStartGrindCandidate")
    tare_requested = low_latency[tare_requested_start:tare_complete_start]
    tare_complete = low_latency[tare_complete_start:tare_complete_end]
    cutoff_start = low_latency.index("static inline void grinderTickGrindingCutoff")
    fresh_start = low_latency.index("static inline void grinderRuntimeFreshWeightTick")
    assert "const uint8_t command[] = { '!', '\\n' };" in low_latency
    assert "grinderRuntime.client.write(command, sizeof(command))" in low_latency
    assert 'return grinderSendSimpleCommand("OFF", GRINDER_COMMAND_OFF);' in low_latency
    assert low_latency.index("grinderSendOff()", cutoff_start) < low_latency.index("[grinder] cutoff", cutoff_start)
    assert low_latency.index("grinderPlugConnectionStale(millis())", fresh_start) < low_latency.index("grinderTickGrindingCutoff(weight);", fresh_start)
    assert_contains(PROTOCOL_HEADER, "GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500")
    assert "GRINDER_CONFIRM_MIN_DURATION_MS 750" in low_latency
    assert "GRINDER_CONFIRM_MIN_RISE_GRAMS 1.0f" in low_latency
    assert "GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES 3" in low_latency
    assert "grinderRuntime.tarePending" in low_latency
    assert "grinderRuntime.setupMassBlocked" in low_latency
    cutoff = low_latency[cutoff_start:fresh_start]
    blocked_start = cutoff.index("if (grinderRuntime.setupMassBlocked)")
    blocked_end = cutoff.index("const uint32_t now")
    blocked = cutoff[blocked_start:blocked_end]
    assert 'grinderSetStatus("tare cup")' not in cutoff
    assert "grinderZeroStable(weight)" in blocked
    assert "grinderResetCutoffGuard()" in blocked
    assert 'grinderSetStatus("ready")' in blocked
    assert "grinderSendOff()" not in blocked
    assert "averageRate > GRINDER_MAX_GRIND_RATE_GPS" in low_latency
    assert "grinderRuntime.grindConfirmed = true" in low_latency
    assert "grinderWeightInPositiveDoseRange(weight)" in low_latency
    assert "grinderCutoffShouldStop" in low_latency
    assert "grinderRuntime.userTareComplete" not in low_latency[cutoff_start:fresh_start]
    assert 'grinderSetStatus("ready");' in runtime
    assert low_latency.index('grinderSetStatus("grinding");') > low_latency.index("grinderRuntime.grindConfirmed = true")
    assert "grinderCutoffGrams(grinderSettings.targetGrams, grinderSettings.safetyMarginGrams)" in low_latency
    assert "now - grinderRuntime.lastCommandAt >= 150" in runtime
    assert "grinderEnterError(\"lost plug\")" in runtime
    assert "grinderRuntime.state == GRINDER_STATE_ERROR" in tare_requested
    assert 'grinderSetStatus("armed");' in tare_complete
    assert 'grinderSetStatus("plug wait");' in tare_complete


def test_pending_command_timeouts_source_contracts():
    runtime = source(RUNTIME_HEADER)
    armed_start = runtime.index("static inline void grinderTickArmed")
    stopping_start = runtime.index("static inline void grinderTickStopping")
    removal_start = runtime.index("static inline bool grinderWeightIndicatesRemoval")
    armed = runtime[armed_start:stopping_start]
    stopping = runtime[stopping_start:removal_start]
    assert "grinderRuntime.pendingCommand == GRINDER_COMMAND_ON" in armed
    assert 'grinderEnterError("on timeout")' in armed
    assert "now - grinderRuntime.stateEnteredAt > 2500" in stopping
    assert "now - grinderRuntime.lastCommandAt >= 150" in stopping
    assert stopping.index("stateEnteredAt > 2500") < stopping.index("grinderSendOff();")


def test_weight_and_loop_source_order():
    hds = source(HDS_SOURCE)
    assert hds.index("f_grinder_fast_weight = tracking_compensated;") < hds.index("float stable_output = applyStableOutput(tracking_compensated);")
    assert "grinderRuntimeFreshWeightTick(f_grinder_fast_weight, grinderFastWeightSequence);" in hds
    assert "grinderRuntimeTick(f_displayedValue);" in hds
    assert hds.index("pureScale();") < hds.index("updateOled();") < hds.rindex("grinderRuntimeTick(f_displayedValue);")
    assert "grinderRuntimeNotifyTareRequested(userRequested);" in hds
    assert "grinderRuntimeNotifyTareComplete();" in hds
    assert 'tareScaleWhenAdcReady("button tare", true)' in hds
    assert 'tareScaleWhenAdcReady("remote tare", true)' in hds
    assert 'tareScaleWhenAdcReady("ADC recovery tare")' in hds
    assert 'tareScaleWhenAdcReady("charging wake tare")' in hds
    assert 'tareScaleWhenAdcReady("ADS reset tare")' in source(ROOT / "include" / "usbcomm.h")


def test_sampling_changes_blocked_during_grinder_cutoff_states():
    low_latency = source(LOW_LATENCY_HEADER)
    assert "grinderRuntimeLocksScaleSampling" in low_latency
    assert "GRINDER_STATE_GRINDING" in low_latency
    assert "GRINDER_STATE_STOPPING" in low_latency
    assert_contains(HDS_SOURCE, "grinderRuntimeLocksScaleSampling()")
    assert_contains(MENU_HEADER, "grinderRuntimeLocksScaleSampling()")


def test_discovery_contracts():
    assert_not_contains(DISCOVERY_HEADER, "MDNS.queryService")
    assert_not_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByMdns")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugs(bool debugRaw = true, uint8_t attempts = 3)")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByRawMdns")
    assert_contains(DISCOVERY_HEADER, "grinderAddDiscoveryFromRawMdnsResult")
    assert_contains(DISCOVERY_HEADER, "grinderResolveRawMdnsIpv4")
    assert_contains(DISCOVERY_HEADER, "grinderStripLocalSuffix(host)")
    assert_contains(DISCOVERY_HEADER, "mdns_query_a(host, 2000, &address)")
    assert_contains(DISCOVERY_HEADER, "return grinderProbeDiscoveryIp(ip, hostname);")
    assert_contains(DISCOVERY_HEADER, "const uint32_t timeouts[] = { 1500, 2500, 3500 }")
    assert_contains(DISCOVERY_HEADER, "wifiEnsureMdnsReadyForSta")
    assert_not_contains(DISCOVERY_HEADER, "WiFi.setSleep")
    assert_not_contains(DISCOVERY_HEADER, "esp_wifi_set_ps")
    assert_not_contains(DISCOVERY_HEADER, "WIFI_PS_NONE")
    assert_contains(DISCOVERY_HEADER, 'mdns_query_ptr("_grinderplug", "_tcp"')
    assert_contains(DISCOVERY_HEADER, "raw mdns ptr err=")
    assert_contains(DISCOVERY_HEADER, "timeout=%lu")
    assert_contains(DISCOVERY_HEADER, "WiFi.RSSI()")
    assert_contains(DISCOVERY_HEADER, "ESP.getFreeHeap()")
    assert_contains(DISCOVERY_HEADER, "grinderDebugRawMdnsQuery")
    assert_not_contains(DISCOVERY_HEADER, 'model != "NOUS_A6T"')
    assert_not_contains(DISCOVERY_HEADER, 'strcmp(model, "NOUS_A6T")')
    assert_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS")
    assert_contains(DISCOVERY_HEADER, "#define GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS 250")
    assert_contains(DISCOVERY_HEADER, "#define GRINDER_DISCOVERY_READ_TIMEOUT_MS 350")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByTcpScan")
    assert_contains(DISCOVERY_HEADER, "grinderProbeDiscoveryIp")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoveryReadLine")
    assert_contains(DISCOVERY_HEADER, "grinderSetStatus(\"scan tcp\")")
    assert_contains(DISCOVERY_HEADER, "grinderProbeDiscoveryIp(grinderSettings.lastIp, grinderSettings.hostname);")
    assert_contains(DISCOVERY_HEADER, "attempt < 3 && grinderRuntime.discoveredCount == 0")
    assert_contains(DISCOVERY_HEADER, "ip != grinderSettings.lastIp")
    assert_contains(DISCOVERY_HEADER, "radius < 255 && grinderRuntime.discoveredCount < 8")
    assert source(DISCOVERY_HEADER).index("grinderDiscoverPlugsByRawMdns(timeouts[timeoutIndex], debugRaw);") < source(DISCOVERY_HEADER).index("grinderDiscoverPlugsByTcpScan();")
    assert source(DISCOVERY_HEADER).index("grinderDiscoverPlugsByTcpScan();") < source(DISCOVERY_HEADER).index("grinderDebugRawMdnsQuery();")
    assert_not_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsLightweight")
    assert_not_contains(DISCOVERY_HEADER, "grinderFindSelectedByMdns")
    assert_not_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK")


def test_firmware_contracts():
    assert_contains(PROTOCOL_HEADER, "GRINDER_TCP_PORT 31980")
    assert_contains(PROTOCOL_HEADER, "grinderParseResponse")
    assert_contains(PROTOCOL_HEADER, "grinderCopyMacPrefix")
    assert_contains(PROTOCOL_HEADER, "strlen(value) < 17")
    assert_contains(PROTOCOL_HEADER, "length != 29 && length != 30")
    assert_contains(PROTOCOL_HEADER, "length < 30 || length > GRINDER_TCP_MAX_LINE_LENGTH")
    assert_contains(PROTOCOL_HEADER, "grinderCutoffGrams")
    assert_contains(PROTOCOL_HEADER, "return targetGrams - safetyMarginGrams;")
    assert_not_contains(PROTOCOL_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, 'preferences.getFloat("latency"')
    assert_not_contains(RUNTIME_HEADER, 'preferences.putFloat("latency"')
    assert_contains(RUNTIME_HEADER, "targetGrams = 15.0f")
    assert_contains(RUNTIME_HEADER, "safetyMarginGrams = 2.0f")
    assert_contains(RUNTIME_HEADER, "grinderNormalizeTargetGrams")
    assert_contains(RUNTIME_HEADER, "grinderNormalizeSafetyGrams")
    assert_contains(RUNTIME_HEADER, 'grinderSetStatus("tare to arm")')
    assert_contains(RUNTIME_HEADER, "bool userTareComplete = false")
    assert_contains(RUNTIME_HEADER, "grinderRuntime.userTareComplete = false")
    assert source(RUNTIME_HEADER).count("grinderRuntime.userTareComplete = false;") == 1
    assert_contains(RUNTIME_HEADER, "grinderRuntime.tareRearmRequested = response.relayOn")
    assert_not_contains(RUNTIME_HEADER, "grind timeout")
    assert_contains(RUNTIME_HEADER, "bool setupMassBlocked = false")
    assert_contains(PROTOCOL_HEADER, "GRINDER_MAX_GRIND_RATE_GPS 6.0f")
    assert_contains(PROTOCOL_HEADER, "GRINDER_MIN_CUTOFF_GRAMS 5.0f")
    assert_contains(RUNTIME_HEADER, 'preferences.getFloat("safety", 2.0f)')
    assert_contains(RUNTIME_HEADER, "previousWifiOnBoot")
    assert_contains(RUNTIME_HEADER, 'preferences.getBool("wifi_prev", false)')
    assert_contains(RUNTIME_HEADER, 'preferences.putBool("wifi_saved", grinderSettings.previousWifiOnBootSaved)')
    assert_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_RECONNECT_INTERVAL_MS 3000")
    assert_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_HOST_RESOLVE_TIMEOUT_MS 250")
    assert_not_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_BACKGROUND_MDNS_INTERVAL_MS")
    assert_not_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_BACKGROUND_MDNS_TIMEOUT_MS")
    assert_contains(RUNTIME_HEADER, 'preferences.begin("grinder"')
    assert_contains(RUNTIME_HEADER, '#include "grinder_runtime_adaptive.h"')
    assert_contains(RUNTIME_HEADER, '#include "grinder_settings_persistence.h"')
    assert_contains(RUNTIME_HEADER, '#include "grinder_discovery.h"')
    assert_contains(RUNTIME_HEADER, '#include "grinder_runtime_low_latency.h"')
    assert_contains(RUNTIME_HEADER, 'preferences.getUInt("safe_count"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe0"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe1"')
    assert_contains(RUNTIME_HEADER, 'preferences.isKey("safe2"')
    assert_contains(RUNTIME_HEADER, 'preferences.putFloat("safe2"')
    assert_contains(RUNTIME_HEADER, "esp_read_mac(mac, ESP_MAC_WIFI_STA)")
    assert_contains(RUNTIME_HEADER, "[grinder] bad response line=")
    assert_contains(RUNTIME_HEADER, "client.connect(ip, GRINDER_TCP_PORT, GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS)")
    assert_contains(RUNTIME_HEADER, 'grinderSetStatus("plug wait")')
    assert_not_contains(RUNTIME_HEADER, "grinderFindSelectedByMdns")
    assert_not_contains(RUNTIME_HEADER, "grinderAttemptMdnsConnect")
    assert_not_contains(RUNTIME_HEADER, "grinderAttemptBackgroundMdnsConnect")
    assert_not_contains(RUNTIME_HEADER, "grinderDiscoverPlugsByTcpScan")
    assert_not_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS")
    assert_not_contains(RUNTIME_HEADER, "MDNS.queryService")
    assert_not_contains(RUNTIME_HEADER, "stateEnteredAt < GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS")
    assert_not_contains(RUNTIME_HEADER, 'grinderEnterError("select plug")')
    assert_not_contains(RUNTIME_HEADER, "WiFi.setSleep")
    assert_not_contains(RUNTIME_HEADER, "esp_wifi_set_ps")
    assert_not_contains(RUNTIME_HEADER, "WIFI_PS_NONE")
    assert_not_contains(RUNTIME_HEADER, "wifiLowLatency")
    assert_not_contains(LOW_LATENCY_HEADER, "WiFi.setSleep")
    assert_not_contains(LOW_LATENCY_HEADER, "esp_wifi_set_ps")
    assert_not_contains(LOW_LATENCY_HEADER, "WIFI_PS_NONE")
    assert_not_contains(LOW_LATENCY_HEADER, "wifiLowLatency")
    assert_contains(PERSISTENCE_HEADER, "grinderFlushSettingsIfDirty")
    assert_contains(PERSISTENCE_HEADER, "grinderSaveLastIpIfChanged")
    assert_contains(PERSISTENCE_HEADER, "grinderSaveLookupHintsIfChanged")
    assert_contains(MENU_HEADER, "menuGrinder")
    assert_contains(MENU_HEADER, "Grinder Plug")
    assert_contains(MENU_HEADER, "grinderFindPlugsForSelection")
    assert_contains(MENU_HEADER, "grinderDrawPlugList")
    assert_contains(MENU_HEADER, "grinderEditNumber")
    assert_contains(MENU_HEADER, "grinderHandleDraftAdjust")
    assert_contains(MENU_HEADER, "bool grinderEnsureWifiReadyForDiscovery()")
    assert_contains(MENU_HEADER, "grinderReleaseClientForDiscovery();")
    assert_contains(RUNTIME_HEADER, "grinderSendOff();")
    assert_contains(RUNTIME_HEADER, "grinderRuntime.client.print(\"BYE\\n\");")
    assert_contains(MENU_HEADER, "if (!b_wifiEnabled) {\n    b_wifiOnBoot = true;\n    wifi_init();")
    assert_contains(MENU_HEADER, "grinderSettings.previousWifiOnBoot = b_wifiOnBoot;")
    assert_contains(MENU_HEADER, "storagePutBool(KEY_WIFI_BOOT, true);")
    assert_contains(MENU_HEADER, "const bool restoreWifiOnBoot = grinderSettings.previousWifiOnBoot;")
    assert_contains(MENU_HEADER, "storagePutBool(KEY_WIFI_BOOT, restoreWifiOnBoot);")
    assert_contains(MENU_HEADER, "WiFi Off deferred until Grinder Off.")
    assert_contains(MENU_HEADER, "step * 10.0f")
    assert_contains(MENU_HEADER, "GRINDER_TARGET_MIN_GRAMS")
    assert_contains(MENU_HEADER, "grinderMaxSafetyGrams(grinderSettings.targetGrams)")
    assert_contains(MENU_HEADER, "grinderResetAdaptiveSafety();")
    assert_contains(PARAMETER_HEADER, "bool b_grinderMenuDirectEntry = false")
    assert_contains(PARAMETER_HEADER, "bool b_buttonChordSuppressUntilRelease = false")
    assert_contains(PARAMETER_HEADER, "extern GrinderSettings grinderSettings")
    assert_contains(PARAMETER_HEADER, "extern GrinderRuntime grinderRuntime")
    assert_contains(MENU_HEADER, "currentSelection->parentMenu == &menuGrinder && b_grinderMenuDirectEntry")
    assert_not_contains(MENU_HEADER, "menuGrinderFind")
    assert_not_contains(MENU_HEADER, "menuGrinderLightScan")
    assert_not_contains(MENU_HEADER, "grinderLightScanMenu")
    assert_not_contains(RUNTIME_HEADER, "grinderCycleTarget")
    assert_contains(HDS_SOURCE, "grinderLoadSettings();")
    assert_contains(HDS_SOURCE, "GRINDER_MENU_CHORD_HOLD_MS 500")
    assert_contains(HDS_SOURCE, "bool handleGrinderMenuChord()")
    assert_contains(HDS_SOURCE, "currentMenu = grinderMenu;")
    assert_contains(HDS_SOURCE, "b_buttonChordSuppressUntilRelease = true")
    assert_contains(HDS_SOURCE, "grinderPauseForMenu();")
    assert_contains(HDS_SOURCE, "grinderRuntimeTick(f_displayedValue);\n      showMenu();")
    assert_contains(HDS_SOURCE, "buttonChecksSuppressedUntilRelease()")
    assert_contains(MENU_HEADER, "grinderResumeAfterMenu();")
    assert_contains(RUNTIME_HEADER, "bool menuPaused = false")
    assert_contains(RUNTIME_HEADER, "static inline void grinderTickWhileMenuOpen(float weight)")
    assert_contains(HDS_SOURCE, "if (!buttonChecksSuppressedUntilRelease() && !handleGrinderMenuChord())")
    assert_contains(HDS_SOURCE, "void beforeDeepSleepFlush()")
    assert_contains(HDS_SOURCE, "grinderFlushSettingsIfDirty();")
    assert_contains(POWER_HEADER, "beforeDeepSleepFlush();")


if __name__ == "__main__":
    test_production_contract()
    test_wrong_mac_runtime_contract()
    test_adaptive_safety_source_contracts()
    test_low_latency_cutoff_source_contracts()
    test_pending_command_timeouts_source_contracts()
    test_weight_and_loop_source_order()
    test_sampling_changes_blocked_during_grinder_cutoff_states()
    test_discovery_contracts()
    test_firmware_contracts()
    print("grinder TCP contract tests passed")
