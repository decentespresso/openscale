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


def test_sinks_forward_the_target_and_refuse_while_busy():
    for header in (BLE_HEADER, USB_HEADER):
        text = source(header)
        assert "void wifiUpdate(const PullOtaTargetVersion &target) {" in text, (
            f"{header.name} sink must take a target"
        )
        assert "if (b_pullOtaRunning || b_ota) {" in text, (
            f"{header.name} must refuse a start while an update runs"
        )
    assert_contains(BLE_HEADER, "if (!remoteQueueWifiUpdate(target)) {")
    assert_contains(USB_HEADER, "::wifiUpdate(target);")
    assert "remoteQueuePending(WSP_WIFI_UPDATE)" not in source(BLE_HEADER), (
        "the BLE sink must queue through remoteQueueWifiUpdate so a target is always written"
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
    assert "if (!(wsPendingMask & WSP_WIFI_UPDATE)) {" in helper, (
        "a second start request must not overwrite an already queued target"
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
