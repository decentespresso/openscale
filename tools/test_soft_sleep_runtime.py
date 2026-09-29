from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    ble = (ROOT / "include/ble.h").read_text(encoding="utf-8")
    ws = (ROOT / "include/websocket.h").read_text(encoding="utf-8")
    hds = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    parameter = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    sleepCommand = ws[ws.index('if (websocketEqualsIgnoreCase(command, "sleep") ||'):]
    loop = block_after(hds, "void loop()")
    sleepGate = loop.index("if (!b_softSleep)")
    wifiGuard = "if (b_softSleep && b_wifiEnabled)"
    assert loop.index(wifiGuard) < sleepGate
    assert loop.index("if (b_ota)") < loop.index(wifiGuard)
    harness = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <mutex>
@COMMANDS@
bool b_softSleep = false, b_u8g2Sleep = false, b_wifiEnabled = true;
bool b_ota = false, b_powerOff = false, pendingOtaDispatching = false;
bool rail = true, accessory = true;
int wakes = 0, sleeps = 0, supervises = 0, polls = 0;
constexpr int PWR_CTRL = 3, ACC_PWR_CTRL = 14, LOW = 0;
uint32_t wsPendingMask = 0;
uint8_t pendingSamplesInUse = 0;
uint8_t pendingOtaTargetMajor = 0, pendingOtaTargetMinor = 0, pendingOtaTargetPatch = 0;
bool pendingOtaTargetPresent = false;
unsigned long pendingResetAt = 0, pendingOtaResetAt = 0;
struct PullOtaTargetVersion { uint8_t major, minor, patch; bool present; };
std::mutex otaDispatchMutex;
int wsPendingMux = 0;
void (*afterSnapshot)() = nullptr;
void portENTER_CRITICAL(int *mux) { assert(*mux == 0); *mux = 1; }
void portEXIT_CRITICAL(int *mux) {
  assert(*mux == 1);
  *mux = 0;
  if (afterSnapshot) {
    const auto callback = afterSnapshot;
    afterSnapshot = nullptr;
    callback();
  }
}
struct {
  void setPowerSave(int) { assert(wsPendingMux == 0); }
  void setContrast(int) {}
} u8g2;
struct { template<class T> void println(T) {} void print(const char*) {} } Serial;
struct { void reset() {} void start() {} void stop() {} } stopWatch;
struct { int getSamplesInUse() { return 1; } } scale;
unsigned long millis() { return 1; }
void reset() { assert(false); }
bool setScaleSamplesInUseWhenReady(uint8_t, const char*) { return true; }
void applyEnergyDisplayCommand(bool) {}
void applyEnergyLowPowerCommand() {}
void recordEnergyActivity() {}
void notifyEnergyMainLoop() {}
void digitalWrite(int pin, int value) {
  assert(wsPendingMux == 0);
  (pin == PWR_CTRL ? rail : accessory) = value;
  if (pin == PWR_CTRL && value == LOW) ++sleeps;
}
void refreshEnergyIdleWakeForRuntimeState() {}
void remoteQueuePending(uint32_t bits) { @QUEUE@ }
void remoteReplacePending(uint32_t setBits, uint32_t clearBits) { @REPLACE@ }
void wsReplacePending(uint32_t setBits, uint32_t clearBits) { @WS_REPLACE@ }
void remoteRestoreDeferredPendingLocked(uint32_t deferredMask, unsigned long resetAt) { @RESTORE@ }
void remoteFinishWifiUpdateDispatch() { @FINISH_OTA@ }
void sendWebsocketStatus(void*, const char*) {}
void wakeScaleFromSoftSleep(const char*) {
  assert(wsPendingMux == 0);
  rail = accessory = true;
  b_softSleep = b_u8g2Sleep = false;
  ++wakes;
}
void wifiSupervise() { ++supervises; }
void wifiConfigServerPoll() { ++polls; }
void bleWake() { @BLE_WAKE@ }
bool wsWake() { void *client = nullptr; @WS_WAKE@ }
void bleSleep() { @BLE_SLEEP@ }
bool wsSleep() { void *client = nullptr; @WS_SLEEP@ }
void dispatch() { @DISPATCH@ }
int transport = 0;
void wakeCommand() { if (transport == 0) bleWake(); else assert(wsWake()); }
void sleepCommand() { if (transport == 0) bleSleep(); else assert(wsSleep()); }
void sleepWakeCommands() { sleepCommand(); wakeCommand(); }
void wakeSleepCommands() { wakeCommand(); sleepCommand(); }
void supervise() { if (b_softSleep && b_wifiEnabled) { @WIFI@ } }
int main() {
  for (transport = 0; transport < 2; ++transport) {
    sleepCommand();
    dispatch();
    assert(b_softSleep && !rail && !accessory);
    wakeCommand();
    assert(b_softSleep && !rail && !accessory);
    const int before = wakes;
    dispatch();
    assert(!b_softSleep && rail && accessory && wakes == before + 1);
    wakeCommand();
    dispatch();
    assert(wakes == before + 1);
    sleepCommand();
    wakeCommand();
    assert(!(wsPendingMask & WSP_SLEEP_ON));
    dispatch();
    assert(!b_softSleep && rail && accessory);

    for (const auto newerWake : {wakeCommand, sleepWakeCommands}) {
      sleepCommand();
      const int sleepsBefore = sleeps;
      afterSnapshot = newerWake;
      dispatch();
      assert(!afterSnapshot);
      assert(!b_softSleep && !b_u8g2Sleep && rail && accessory);
      assert(sleeps == sleepsBefore && wsPendingMask == WSP_SLEEP_OFF);
      dispatch();
      assert(!b_softSleep && rail && accessory && wsPendingMask == 0);
    }

    sleepCommand();
    dispatch();
    for (const auto newerSleep : {sleepCommand, wakeSleepCommands}) {
      wakeCommand();
      const int wakesBefore = wakes;
      afterSnapshot = newerSleep;
      dispatch();
      assert(!afterSnapshot);
      assert(b_softSleep && b_u8g2Sleep && !rail && !accessory);
      assert(wakes == wakesBefore && wsPendingMask == WSP_SLEEP_ON);
      dispatch();
      assert(b_softSleep && wsPendingMask == 0);
    }
    wakeCommand();
    dispatch();
    assert(!b_softSleep && rail && accessory);

    sleepCommand();
    remoteQueuePending(WSP_POWER_OFF);
    afterSnapshot = wakeCommand;
    dispatch();
    assert(!b_softSleep && rail && accessory && b_powerOff);
    assert(wsPendingMask == WSP_SLEEP_OFF);
    b_powerOff = false;
    dispatch();
    assert(wsPendingMask == 0);
  }
  b_softSleep = true;
  supervise();
  assert(supervises == 1);
  assert(polls == (HDS_FEATURE_WEBSERVER ? 0 : 1));
  b_wifiEnabled = false;
  supervise();
  assert(supervises == 1);
  assert(polls == (HDS_FEATURE_WEBSERVER ? 0 : 1));
  b_wifiEnabled = true;
  b_softSleep = false;
  supervise();
  assert(supervises == 1);
  assert(polls == (HDS_FEATURE_WEBSERVER ? 0 : 1));
}
'''
    replacements = {
        "COMMANDS": parameter[parameter.index("const uint32_t WSP_DISPLAY_ON"):parameter.index("portMUX_TYPE wsPendingMux")],
        "QUEUE": block_after(ws, "inline void remoteQueuePending("),
        "REPLACE": block_after(ws, "inline void remoteReplacePending("),
        "WS_REPLACE": block_after(ws, "inline void wsReplacePending("),
        "RESTORE": block_after(ws, "inline void remoteRestoreDeferredPendingLocked("),
        "FINISH_OTA": block_after(ws, "inline void remoteFinishWifiUpdateDispatch("),
        "BLE_WAKE": block_after(ble, "void softSleepOff()"),
        "WS_WAKE": block_after(sleepCommand, 'if (websocketEqualsIgnoreCase(action, "off") ||'),
        "BLE_SLEEP": block_after(ble, "void softSleepOn()"),
        "WS_SLEEP": block_after(sleepCommand, 'if (websocketEqualsIgnoreCase(action, "on"))'),
        "DISPATCH": block_after(ws, "void processWsPendingCmds()"),
        "WIFI": block_after(loop, wifiGuard),
    }
    for marker, source in replacements.items():
        harness = harness.replace(f"@{marker}@", source)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "check.cpp"
        source.write_text(harness, encoding="utf-8")
        for energy, webserver in ((0, 0), (0, 1), (1, 0), (1, 1)):
            binary = Path(directory) / "check.exe"
            subprocess.run(["g++", "-std=c++17", f"-DHDS_ENABLE_ENERGY_MENU={energy}",
                            f"-DHDS_FEATURE_WEBSERVER={webserver}", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Soft-sleep transport dispatch and sleeping WiFi supervision passed")


if __name__ == "__main__":
    main()
