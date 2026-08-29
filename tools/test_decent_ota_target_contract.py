import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TARGET_HEADER = ROOT / "include" / "pull_ota_target.h"
PROTOCOL_HEADER = ROOT / "include" / "decent_protocol.h"
FRAME_HEADER = ROOT / "include" / "decent_protocol_frame.h"
BLE_HEADER = ROOT / "include" / "ble.h"
USB_HEADER = ROOT / "include" / "usbcomm.h"
WEBSOCKET_HEADER = ROOT / "include" / "websocket.h"
PARAMETER_HEADER = ROOT / "include" / "parameter.h"
HOST_CONTRACT = ROOT / "tools" / "test_pull_ota_target_contract.cpp"


def source(path):
    return path.read_text(encoding="utf-8")


def assert_contains(path, text):
    assert text in source(path), f"{path.name} missing {text!r}"


def test_target_helpers_compile_and_pass():
    assert HOST_CONTRACT.exists()
    compiler = next((shutil.which(name) for name in ("g++", "clang++") if shutil.which(name)), None)
    if compiler is None:
        print("native C++ OTA target contract skipped: no host compiler")
        return
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / "pull_ota_target_contract"
        subprocess.run(
            [compiler, "-std=c++11", "-I", str(ROOT / "include"), str(HOST_CONTRACT), "-o", str(executable)],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def test_payload_encoding_keeps_version_bytes_out_of_frame_start():
    assert_contains(TARGET_HEADER, "HDS_OTA_TARGET_BYTE_BIAS = 0x80")
    assert_contains(TARGET_HEADER, "HDS_OTA_TARGET_MAX_COMPONENT = 127")
    assert_contains(TARGET_HEADER, "HDS_OTA_TARGET_PAYLOAD_BYTES = 3")


def test_framer_uses_high_bit_to_detect_the_payload():
    frame = source(FRAME_HEADER)
    marker = "    case 0x1B:\n      if (len >= 3 && pullOtaTargetByteIsBiased(data[2])) {\n"
    assert marker in frame, "0x1B framing must key off the biased payload byte"
    assert "return decentFixedFrameLength(len, 2 + HDS_OTA_TARGET_PAYLOAD_BYTES);" in frame
    frame_case = frame[frame.index(marker):]
    frame_case = frame_case[: frame_case.index("#ifdef BUZZER")]
    assert "return decentFixedFrameLength(len, 2);" in frame_case, (
        "a bare 0x1B must still frame as two bytes with no timeout dependence"
    )
    assert "if (!decentTargetedOtaPayloadIsIntact(data, len)) {\n          return 1;\n" in frame_case, (
        "a targeted payload interrupted by a new frame boundary must be resynced, not consumed"
    )
    assert "pullOtaTargetBytesAreBiased(data + 3, available - 3)" in frame, (
        "every payload byte received so far must carry the bias"
    )
    assert "decentCommandFrameLength" not in source(PROTOCOL_HEADER), (
        "the framer lives in decent_protocol_frame.h; do not reintroduce a copy"
    )


def test_dispatch_refuses_a_started_but_short_payload():
    protocol = source(PROTOCOL_HEADER)
    start = protocol.index("    case 0x1B: {")
    end = protocol.index("#ifdef BUZZER", start)
    dispatch = protocol[start:end]
    assert "pullOtaTargetByteIsBiased(data[2])" in dispatch
    assert '"WiFi update version"' in dispatch
    assert "pullOtaTargetFromBiasedBytes(data + 2)" in dispatch
    assert "sink.wifiUpdate(target)" in dispatch
    assert dispatch.index("decentRequireLength") < dispatch.index("pullOtaTargetFromBiasedBytes"), (
        "length must be checked before the payload is decoded"
    )
    assert "pullOtaTargetBytesAreBiased(data + 2, HDS_OTA_TARGET_PAYLOAD_BYTES)" in dispatch, (
        "a payload holding an unbiased byte must be refused instead of decoded"
    )
    assert dispatch.index("pullOtaTargetBytesAreBiased") < dispatch.index("pullOtaTargetFromBiasedBytes"), (
        "the bias check must run before the payload is decoded"
    )


def test_sinks_forward_the_target_and_refuse_while_busy():
    for header in (BLE_HEADER, USB_HEADER):
        text = source(header)
        assert "void wifiUpdate(const PullOtaTargetVersion &target) {" in text, (
            f"{header.name} sink must take a target"
        )
        assert "if (b_pullOtaRunning || b_ota) {" in text, (
            f"{header.name} must refuse a start while an update runs"
        )
    for header in (BLE_HEADER, USB_HEADER):
        assert_contains(header, "if (!remoteQueueWifiUpdate(target)) {")
        assert "remoteQueuePending(WSP_WIFI_UPDATE)" not in source(header), (
            f"{header.name} must queue through remoteQueueWifiUpdate so a target is always written"
        )
    assert "::wifiUpdate(target);" not in source(USB_HEADER), (
        "USB must not start an update directly; a queued request would then be overtaken"
    )


def test_pending_target_state_is_volatile_and_mutex_guarded():
    parameter = source(PARAMETER_HEADER)
    for name in (
        "pendingOtaTargetMajor",
        "pendingOtaTargetMinor",
        "pendingOtaTargetPatch",
        "pendingOtaTargetPresent",
        "requestedOtaTargetMajor",
        "requestedOtaTargetMinor",
        "requestedOtaTargetPatch",
        "requestedOtaTargetPresent",
    ):
        assert f"volatile uint8_t {name}" in parameter or f"volatile bool {name}" in parameter, (
            f"{name} must be declared volatile in parameter.h"
        )

    websocket = source(WEBSOCKET_HEADER)
    start = websocket.index("inline bool remoteQueueWifiUpdate(")
    end = websocket.index("inline void wsQueuePending(", start)
    helper = websocket[start:end]
    assert "portENTER_CRITICAL(&wsPendingMux);" in helper
    assert "portEXIT_CRITICAL(&wsPendingMux);" in helper
    assert helper.index("pendingOtaTargetPresent = target.present;") < helper.index("portEXIT_CRITICAL")
    assert "wsPendingMask |= WSP_WIFI_UPDATE;" in helper
    assert "if (!(wsPendingMask & WSP_WIFI_UPDATE) && !pendingOtaDispatching &&\n      !b_ota && !b_pullOtaRunning) {" in helper, (
        "admission must refuse a request that is queued, dispatching, or already running"
    )
    for forbidden in ("u8g2", "LittleFS", "WiFi.", "pullOtaUpdate", "delay("):
        assert forbidden not in helper, f"remoteQueueWifiUpdate must not call {forbidden}"


def test_dispatch_snapshots_the_target_inside_the_critical_section():
    websocket = source(WEBSOCKET_HEADER)
    start = websocket.index("void processWsPendingCmds() {")
    snapshot = websocket[start : websocket.index("portEXIT_CRITICAL", start)]
    assert "otaTarget.present = pendingOtaTargetPresent;" in snapshot, (
        "the pending target must be copied out before the critical section is released"
    )
    assert "wifiUpdate(otaTarget);" in websocket


def test_dispatch_holds_the_request_until_the_update_is_running():
    parameter = source(PARAMETER_HEADER)
    assert "volatile bool pendingOtaDispatching = false;" in parameter, (
        "the dispatch handoff flag must be volatile in parameter.h"
    )

    websocket = source(WEBSOCKET_HEADER)
    start = websocket.index("void processWsPendingCmds() {")
    snapshot = websocket[start : websocket.index("portEXIT_CRITICAL", start)]
    assert "pendingOtaDispatching = true;" in snapshot, (
        "clearing WSP_WIFI_UPDATE must mark the request as dispatching in the same critical section"
    )

    body = websocket[start : websocket.index("#if HDS_FEATURE_WEBSOCKET", start)]
    deferral = body[body.index("wsPendingMask |= deferredMask & ~WSP_WIFI_UPDATE;") :]
    assert "pendingOtaDispatching = false;" in deferral[: deferral.index("mask &= WSP_OTA_RESET;")], (
        "a request deferred back to the queue must not stay marked as dispatching"
    )
    dispatch = body[body.index("if (mask & WSP_WIFI_UPDATE) {") :]
    assert dispatch.index("wifiUpdate(otaTarget);") < dispatch.index("remoteFinishWifiUpdateDispatch();"), (
        "the dispatching mark must be cleared only after the update has been started"
    )


def test_a_request_that_cannot_dispatch_is_dropped_rather_than_requeued():
    websocket = source(WEBSOCKET_HEADER)
    start = websocket.index("void processWsPendingCmds() {")
    body = websocket[start : websocket.index("#if HDS_FEATURE_WEBSOCKET", start)]
    assert "wsPendingMask |= deferredMask & ~WSP_WIFI_UPDATE;" in body, (
        "a start request deferred while an update runs must be dropped, not left to fire later"
    )
    assert "droppedWifiUpdate" in body, "the dropped request must be logged"


def test_websocket_command_is_guarded_and_answers_accept_time_refusals():
    websocket = source(WEBSOCKET_HEADER)
    start = websocket.index('if (websocketEqualsIgnoreCase(command, "wifi_update")) {')
    branch = websocket[start : websocket.index("return false;", start)]
    assert '"ota_busy"' in branch
    assert '"ota_version_invalid"' in branch
    assert "pullOtaParseTargetVersion(action.data(), action.size(), target)" in branch
    assert "if (!remoteQueueWifiUpdate(target)) {" in branch, (
        "the websocket branch must report a refusal when a request is already queued"
    )
    assert 'sendWebsocketStatus(client, "ok");' in branch
    guard = websocket[websocket.rindex("#if HDS_FEATURE_PULL_OTA", 0, start) : start]
    assert guard.strip() == "#if HDS_FEATURE_PULL_OTA", (
        "the wifi_update branch must be compiled out when pull OTA is disabled"
    )
    assert '#if HDS_FEATURE_PULL_OTA\n      || websocketEqualsIgnoreCase(command, "wifi_update")' in websocket, (
        "the energy-activity entry must be compiled out when pull OTA is disabled"
    )
    activity_start = websocket.index('if (websocketEqualsIgnoreCase(command, "tare") ||')
    activity = websocket[activity_start : websocket.index("recordEnergyActivity();", activity_start)]
    assert 'websocketEqualsIgnoreCase(command, "wifi_update")' in activity, (
        "wifi_update must record energy activity like the other control commands"
    )


def main():
    for name, function in sorted(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("Decent OTA target contract tests passed")


if __name__ == "__main__":
    main()
