import re
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
MAC_PATTERN = re.compile(r"^[0-9A-F]{2}(:[0-9A-F]{2}){5}$")


def source(path):
    return path.read_text(encoding="utf-8")


def assert_contains(path, text):
    assert text in source(path), f"{path} missing {text!r}"


def assert_not_contains(path, text):
    assert text not in source(path), f"{path} unexpectedly contains {text!r}"


def parse_response(line):
    parts = line.split()
    if len(parts) == 3 and parts[0] == "OK" and MAC_PATTERN.match(parts[1]):
        state = parts[2].removeprefix("state=")
        if state in {"ON", "OFF"}:
            return {"kind": "OK", "mac": parts[1], "relay_on": state == "ON"}
    if len(parts) == 2 and parts[0] == "BUSY" and MAC_PATTERN.match(parts[1]):
        return {"kind": "BUSY", "mac": parts[1], "relay_on": False}
    if len(parts) == 3 and parts[0] == "ERR" and MAC_PATTERN.match(parts[1]) and parts[2].startswith("reason="):
        return {"kind": "ERR", "mac": parts[1], "reason": parts[2][7:], "relay_on": False}
    return None


def cutoff(target, safety):
    return target - safety


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def adaptive_recommendation(current_safety, target, start_weight, final_weight, duration_ms, history, final_locked=True, valid=True):
    if not valid or not final_locked:
        return current_safety, history, False
    if duration_ms < 500:
        return current_safety, history, False
    span = final_weight - start_weight
    if span < 2.0:
        return current_safety, history, False
    average_rate = span / (duration_ms / 1000.0)
    if average_rate <= 0.0 or average_rate > 6.0:
        return current_safety, history, False
    error = final_weight - target
    if abs(error) > 5.0:
        return current_safety, history, False
    recommendation = clamp(current_safety + clamp(error, -2.0, 2.0), 0.0, 10.0)
    next_history = (history + [recommendation])[-3:]
    return sum(next_history) / len(next_history), next_history, True


def test_response_parser_and_wrong_mac():
    assert parse_response("OK A4:C1:38:12:34:56 state=OFF") == {
        "kind": "OK",
        "mac": "A4:C1:38:12:34:56",
        "relay_on": False,
    }
    assert parse_response("OK A4:C1:38:12:34:56 state=ON")["relay_on"]
    assert parse_response("BUSY A4:C1:38:12:34:56")["kind"] == "BUSY"
    assert parse_response("ERR A4:C1:38:12:34:56 reason=bad_mac")["reason"] == "bad_mac"
    assert parse_response("NOPE A4:C1:38:12:34:56 state=OFF") is None
    assert_contains(RUNTIME_HEADER, "if (!grinderResponseMatchesSelection(response))")
    assert_contains(RUNTIME_HEADER, 'grinderEnterError("wrong mac")')


def test_cutoff_math():
    assert abs(cutoff(20.0, 0.2) - 19.8) < 0.0001
    assert abs(cutoff(15.0, 2.0) - 13.0) < 0.0001


def test_adaptive_safety_math():
    safety, history, learned = adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 5000, [])
    assert learned
    assert abs(safety - 0.4) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 14.9, 5000, history)
    assert learned
    assert abs(safety - 0.35) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 15.1, 5000, history)
    assert learned
    assert abs(safety - 0.3833333) < 0.0001
    safety, history, learned = adaptive_recommendation(safety, 15.0, 1.0, 15.4, 5000, history)
    assert learned
    assert abs(safety - 0.5111111) < 0.0001


def test_adaptive_safety_rejects_bad_samples():
    assert adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 1000, [])[2] is False
    assert adaptive_recommendation(0.2, 15.0, 1.0, 2.0, 5000, [])[2] is False
    assert adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 1000, [], final_locked=False)[2] is False
    assert adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 2000, [], valid=False)[2] is False
    assert adaptive_recommendation(0.2, 15.0, 1.0, 15.2, 2000, [])[2] is False


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
    cutoff_start = low_latency.index("static inline void grinderTickGrindingCutoff")
    fresh_start = low_latency.index("static inline void grinderRuntimeFreshWeightTick")
    assert "const uint8_t command[] = { '!', '\\n' };" in low_latency
    assert "grinderRuntime.client.write(command, sizeof(command))" in low_latency
    assert 'return grinderSendSimpleCommand("OFF", GRINDER_COMMAND_OFF);' in low_latency
    assert low_latency.index("grinderSendOff()", cutoff_start) < low_latency.index("[grinder] cutoff", cutoff_start)
    assert low_latency.index("grinderPlugConnectionStale(millis())", fresh_start) < low_latency.index("grinderTickGrindingCutoff(weight);", fresh_start)
    assert "GRINDER_CUTOFF_ZERO_EXIT_PROTECTION_MS 1500" in low_latency
    assert "GRINDER_CONFIRM_MIN_DURATION_MS 1500" in low_latency
    assert "GRINDER_CONFIRM_MIN_RISE_GRAMS 2.0f" in low_latency
    assert "GRINDER_CONFIRM_MIN_POSITIVE_SAMPLES 4" in low_latency
    assert "GRINDER_CONFIRM_MAX_INITIAL_GRAMS 3.0f" in low_latency
    assert "grinderRuntime.tarePending" in low_latency
    assert "grinderRuntime.setupMassBlocked = true" in low_latency
    assert "grinderRuntime.grindConfirmed = true" in low_latency
    assert "grinderCutoffGrams(grinderSettings.targetGrams, grinderSettings.safetyMarginGrams)" in low_latency
    assert "now - grinderRuntime.lastCommandAt >= 150" in runtime
    assert "grinderEnterError(\"lost plug\")" in runtime


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
    assert hds.index("pureScale();") < hds.index("updateOled();") < hds.index("grinderRuntimeTick(f_displayedValue);")
    assert "grinderRuntimeNotifyTareRequested();" in hds
    assert "grinderRuntimeNotifyTareComplete();" in hds


def test_sampling_changes_blocked_during_grinder_cutoff_states():
    low_latency = source(LOW_LATENCY_HEADER)
    assert "grinderRuntimeLocksScaleSampling" in low_latency
    assert "GRINDER_STATE_GRINDING" in low_latency
    assert "GRINDER_STATE_STOPPING" in low_latency
    assert "GRINDER_STATE_FINDING_PLUG" in low_latency
    assert_contains(HDS_SOURCE, "grinderRuntimeLocksScaleSampling()")
    assert_contains(MENU_HEADER, "grinderRuntimeLocksScaleSampling()")


def test_discovery_contracts():
    assert_contains(DISCOVERY_HEADER, 'MDNS.queryService("grinderplug", "tcp")')
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugs(bool debugRaw = true, uint8_t attempts = 3)")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByRawMdns")
    assert_contains(DISCOVERY_HEADER, "grinderAddDiscoveryFromRawMdnsResult")
    assert_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByRawMdns(350, false)")
    assert_contains(DISCOVERY_HEADER, "wifiEnsureMdnsReadyForSta")
    assert_contains(DISCOVERY_HEADER, "WiFi.setSleep(false)")
    assert_contains(DISCOVERY_HEADER, "grinderRuntime.wifiLowLatency = true")
    assert_contains(DISCOVERY_HEADER, "grinderMaintainWifiLatencyMode();")
    assert_contains(DISCOVERY_HEADER, 'mdns_query_ptr("_grinderplug", "_tcp"')
    assert_contains(DISCOVERY_HEADER, "grinderDebugRawMdnsQuery")
    assert_contains(DISCOVERY_HEADER, 'model != "NOUS_A6T"')
    assert_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS")
    assert_not_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsLightweight")
    assert_not_contains(DISCOVERY_HEADER, "grinderFindSelectedByMdns")
    assert_not_contains(DISCOVERY_HEADER, "GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK")
    assert_not_contains(DISCOVERY_HEADER, "grinderDiscoverPlugsByTcpScan")
    assert_not_contains(DISCOVERY_HEADER, "grinderProbeDiscoveryIp")
    assert_not_contains(DISCOVERY_HEADER, "grinderDiscoveryReadLine")


def test_firmware_contracts():
    assert_contains(PROTOCOL_HEADER, "GRINDER_TCP_PORT 31980")
    assert_contains(PROTOCOL_HEADER, "grinderParseResponse")
    assert_contains(PROTOCOL_HEADER, "grinderCopyMacPrefix")
    assert_contains(PROTOCOL_HEADER, "grinderCutoffGrams")
    assert_contains(PROTOCOL_HEADER, "return targetGrams - safetyMarginGrams;")
    assert_not_contains(PROTOCOL_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, "effectiveLatencySeconds")
    assert_not_contains(RUNTIME_HEADER, 'preferences.getFloat("latency"')
    assert_not_contains(RUNTIME_HEADER, 'preferences.putFloat("latency"')
    assert_contains(RUNTIME_HEADER, "targetGrams = 15.0f")
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
    assert_not_contains(RUNTIME_HEADER, "GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS")
    assert_not_contains(RUNTIME_HEADER, "MDNS.queryService")
    assert_not_contains(RUNTIME_HEADER, "stateEnteredAt < GRINDER_RUNTIME_STARTUP_DISCOVERY_DELAY_MS")
    assert_not_contains(RUNTIME_HEADER, 'grinderEnterError("select plug")')
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
    assert_contains(MENU_HEADER, "if (!b_wifiEnabled) {\n    b_wifiOnBoot = true;\n    wifi_init();")
    assert_contains(MENU_HEADER, "step * 10.0f")
    assert_contains(MENU_HEADER, "grinderResetAdaptiveSafety();")
    assert_contains(MENU_HEADER, "bool b_grinderMenuDirectEntry = false")
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
    assert_contains(HDS_SOURCE, "buttonChecksSuppressedUntilRelease()")
    assert_contains(HDS_SOURCE, "if (!buttonChecksSuppressedUntilRelease() && !handleGrinderMenuChord())")
    assert_contains(HDS_SOURCE, "void beforeDeepSleepFlush()")
    assert_contains(HDS_SOURCE, "grinderFlushSettingsIfDirty();")
    assert_contains(POWER_HEADER, "beforeDeepSleepFlush();")


if __name__ == "__main__":
    test_response_parser_and_wrong_mac()
    test_cutoff_math()
    test_adaptive_safety_math()
    test_adaptive_safety_rejects_bad_samples()
    test_adaptive_safety_source_contracts()
    test_low_latency_cutoff_source_contracts()
    test_pending_command_timeouts_source_contracts()
    test_weight_and_loop_source_order()
    test_sampling_changes_blocked_during_grinder_cutoff_states()
    test_discovery_contracts()
    test_firmware_contracts()
    print("grinder TCP contract tests passed")
