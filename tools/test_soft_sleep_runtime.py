from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    ble = (ROOT / "include/ble.h").read_text(encoding="utf-8")
    usb = (ROOT / "include/usbcomm.h").read_text(encoding="utf-8")
    ws = (ROOT / "include/websocket.h").read_text(encoding="utf-8")
    hds = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    parameter = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    sleepCommand = ws[ws.index('if (websocketEqualsIgnoreCase(command, "sleep") ||'):]
    displayCommand = ws[ws.index('if (websocketEqualsIgnoreCase(command, "display"))'):]
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
bool rail = true, accessory = true, oled = true;
int wakes = 0, sleeps = 0, supervises = 0, adcRefreshes = 0, outputResets = 0;
constexpr int PWR_CTRL = 3, ACC_PWR_CTRL = 14, LOW = 0, HIGH = 1;
uint32_t wsPendingMask = 0;
uint8_t pendingSamplesInUse = 0;
uint8_t pendingOtaTargetMajor = 0, pendingOtaTargetMinor = 0, pendingOtaTargetPatch = 0;
bool pendingOtaTargetPresent = false;
unsigned long pendingResetAt = 0, pendingOtaResetAt = 0;
struct PullOtaTargetVersion { uint8_t major, minor, patch; bool present; };
std::mutex otaDispatchMutex;
int wsPendingMux = 0;
void (*afterSnapshot)() = nullptr;
void (*afterAdcRefresh)() = nullptr;
void (*duringDisplay)() = nullptr;
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
  void setPowerSave(int value) {
    assert(wsPendingMux == 0);
    oled = value == 0;
    if (duringDisplay) {
      const auto callback = duringDisplay;
      duringDisplay = nullptr;
      callback();
    }
  }
  void setContrast(int) { assert(wsPendingMux == 0); }
} u8g2;
struct { template<class T> void println(T) {} void print(const char*) {} } Serial;
struct { void reset() {} void start() {} void stop() {} } stopWatch;
struct {
  int getSamplesInUse() { return 1; }
  void powerUp() { assert(wsPendingMux == 0); ++wakes; }
} scale;
enum class DisplayIdleMode { Active, Off };
struct {
  bool explicitDisplayOff = false;
  DisplayIdleMode displayMode = DisplayIdleMode::Active;
  int requestedDisplayContrast = 255;
  struct { void invalidate() {} } oledFrames;
} energyRuntime;
struct { void recordActivity(unsigned long) {} } energyPolicy;
struct EnergyRuntimePolicy {
  static int displayContrast(int contrast, DisplayIdleMode) { return contrast; }
};
unsigned long millis() { return 1; }
void delay(int) { assert(wsPendingMux == 0); }
void reset() { assert(false); }
bool setScaleSamplesInUseWhenReady(uint8_t, const char*) { return true; }
void applyEnergyLowPowerCommand() {}
void recordEnergyActivity() {}
void clearPendingEnergyActivity() {}
void notifyEnergyMainLoop() {}
void digitalWrite(int pin, int value) {
  assert(wsPendingMux == 0);
  (pin == PWR_CTRL ? rail : accessory) = value;
  if (pin == PWR_CTRL && value == LOW) ++sleeps;
}
void refreshEnergyIdleWakeForRuntimeState() {}
void requestEnergyDisplay(bool enabled) { @REQUEST_DISPLAY@ }
void applyEnergyDisplayMode(DisplayIdleMode requested, bool force = false) { @DISPLAY_MODE@ }
void applyEnergyDisplayCommand(bool enabled) { @DISPLAY_COMMAND@ }
void remoteQueuePending(uint32_t bits) { @QUEUE@ }
void remoteReplacePending(uint32_t setBits, uint32_t clearBits) { @REPLACE@ }
void wsReplacePending(uint32_t setBits, uint32_t clearBits) { @WS_REPLACE@ }
void remoteRestoreDeferredPendingLocked(uint32_t deferredMask, unsigned long resetAt) { @RESTORE@ }
void remoteFinishWifiUpdateDispatch() { @FINISH_OTA@ }
void sendWebsocketStatus(void*, const char*) {}
void queueBleStatusResponse() {}
bool refreshScaleDatasetAfterDiscontinuity(const char*) {
  assert(wsPendingMux == 0);
  ++adcRefreshes;
  if (afterAdcRefresh) {
    const auto callback = afterAdcRefresh;
    afterAdcRefresh = nullptr;
    callback();
  }
  return true;
}
void resetScaleOutputAfterAdcDiscontinuity() { ++outputResets; }
bool wakeScaleFromSoftSleep(const char *context) { @WAKE_HELPER@ }
void wifiSupervise() { ++supervises; }
void bleWake() { @BLE_WAKE@ }
bool wsWake() { void *client = nullptr; @WS_WAKE@ }
void bleSleep() { @BLE_SLEEP@ }
bool wsSleep() { void *client = nullptr; @WS_SLEEP@ }
void usbSleep() { @USB_SLEEP@ }
void usbWake() { @USB_WAKE@ }
void bleDisplayOn() { @BLE_DISPLAY_ON@ }
void bleDisplayOff() { @BLE_DISPLAY_OFF@ }
bool wsDisplayOn() { void *client = nullptr; @WS_DISPLAY_ON@ }
bool wsDisplayOff() { void *client = nullptr; @WS_DISPLAY_OFF@ }
void dispatch() { @DISPATCH@ }
int transport = 0;
void wakeCommand() {
  if (transport == 0) bleWake();
  else if (transport == 1) assert(wsWake());
  else usbWake();
}
void sleepCommand() {
  if (transport == 0) bleSleep();
  else if (transport == 1) assert(wsSleep());
  else usbSleep();
}
void displayOnCommand() { if (transport == 0) bleDisplayOn(); else assert(wsDisplayOn()); }
void displayOffCommand() { if (transport == 0) bleDisplayOff(); else assert(wsDisplayOff()); }
void sleepWakeCommands() { sleepCommand(); wakeCommand(); }
void wakeSleepCommands() { wakeCommand(); sleepCommand(); }
void supervise() { if (b_softSleep && b_wifiEnabled) { @WIFI@ } }
void assertDisplay(bool enabled) {
  assert(oled == enabled && b_u8g2Sleep == !enabled);
#if HDS_ENABLE_ENERGY_MENU
  assert(energyRuntime.displayMode == (enabled ? DisplayIdleMode::Active : DisplayIdleMode::Off));
#endif
}
int main() {
  for (transport = 0; transport < 3; ++transport) {
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

    for (const bool sleeping : {false, true}) {
      displayOnCommand();
      dispatch();
      if (sleeping) { sleepCommand(); dispatch(); }
      const int wakesBefore = wakes;
      const int refreshesBefore = adcRefreshes;
      wakeCommand();
      displayOffCommand();
      dispatch();
      assert(!b_softSleep && rail && accessory);
      assertDisplay(false);
      assert(wakes == wakesBefore + sleeping && adcRefreshes == refreshesBefore + sleeping);
      assert(outputResets == adcRefreshes);
      wakeCommand();
      dispatch();
      assert(oled && !b_u8g2Sleep && wakes == wakesBefore + sleeping);

      displayOnCommand();
      dispatch();
      if (sleeping) { sleepCommand(); dispatch(); }
      displayOffCommand();
      wakeCommand();
      dispatch();
      assert(!b_softSleep && rail && accessory);
      assert(oled == !(sleeping && HDS_ENABLE_ENERGY_MENU));
      assert(b_u8g2Sleep == (sleeping && HDS_ENABLE_ENERGY_MENU));

      for (const bool enabled : {false, true}) {
        displayOnCommand();
        dispatch();
        if (sleeping) { sleepCommand(); dispatch(); }
        wakeCommand();
        if (enabled) { displayOffCommand(); displayOnCommand(); }
        else { displayOnCommand(); displayOffCommand(); }
        dispatch();
        assert(!b_softSleep && rail && accessory);
        assertDisplay(enabled);
      }
    }

    for (const bool sleepFirst : {false, true}) {
      if (sleepFirst) { sleepCommand(); displayOnCommand(); }
      else { displayOnCommand(); sleepCommand(); }
      dispatch();
      assert(b_softSleep && b_u8g2Sleep && !oled && !rail && !accessory);
      wakeCommand();
      dispatch();
      assert(!b_softSleep && rail && accessory);
      assertDisplay(true);
    }

    for (const auto newerWake : {wakeCommand, sleepWakeCommands}) {
      sleepCommand();
      displayOnCommand();
      const int sleepsBefore = sleeps;
      duringDisplay = newerWake;
      dispatch();
      assert(!duringDisplay);
      assert(!b_softSleep && !b_u8g2Sleep && rail && accessory && oled);
      assert(sleeps == sleepsBefore && wsPendingMask == WSP_SLEEP_OFF);
      dispatch();
      assert(!b_softSleep && rail && accessory && wsPendingMask == 0);
    }

    for (const bool enabled : {false, true}) {
      wakeCommand();
      displayOffCommand();
      afterSnapshot = enabled ? displayOnCommand : displayOffCommand;
      dispatch();
      assert(!afterSnapshot && wsPendingMask == (enabled ? WSP_DISPLAY_ON : WSP_DISPLAY_OFF));
      dispatch();
      assertDisplay(enabled);
      assert(wsPendingMask == 0);
    }

    displayOnCommand();
    dispatch();
    sleepCommand();
    dispatch();
    wakeCommand();
    afterAdcRefresh = displayOffCommand;
    dispatch();
    assert(!afterAdcRefresh && !b_softSleep && rail && accessory);
    assert(wsPendingMask == WSP_DISPLAY_OFF);
    dispatch();
    assertDisplay(false);
    displayOnCommand();
    dispatch();
  }
  for (int olderTransport = 0; olderTransport < 3; ++olderTransport) {
    for (int newerTransport = 0; newerTransport < 3; ++newerTransport) {
      transport = olderTransport;
      displayOnCommand();
      sleepCommand();
      dispatch();
      const int wakesBefore = wakes;
      sleepCommand();
      displayOffCommand();
      transport = newerTransport;
      wakeCommand();
      dispatch();
      assert(!b_softSleep && rail && accessory && wakes == wakesBefore + 1);
      assert(oled == !HDS_ENABLE_ENERGY_MENU);
      assert(wsPendingMask == 0);
      wakeCommand();
      dispatch();
      transport = olderTransport;
      displayOnCommand();
      wakeCommand();
      transport = newerTransport;
      sleepCommand();
      dispatch();
      assert(b_softSleep && !rail && !accessory);
      assert(b_u8g2Sleep && !oled && wakes == wakesBefore + 1);
      assert(wsPendingMask == 0);
      wakeCommand();
      dispatch();
    }
  }
  for (int cycle = 0; cycle < 3; ++cycle) {
    const int wakesBefore = wakes;
    const int refreshesBefore = adcRefreshes;
    usbSleep();
    dispatch();
    assert(b_softSleep && b_u8g2Sleep && !oled && !rail && !accessory);
    usbWake();
    dispatch();
    assert(!b_softSleep && rail && accessory);
    assertDisplay(true);
    assert(wakes == wakesBefore + 1 && adcRefreshes == refreshesBefore + 1);
  }
  b_softSleep = true;
  supervise();
  assert(supervises == 1);
  b_wifiEnabled = false;
  supervise();
  assert(supervises == 1);
  b_wifiEnabled = true;
  b_softSleep = false;
  supervise();
  assert(supervises == 1);
}
'''
    replacements = {
        "COMMANDS": parameter[parameter.index("const uint32_t WSP_DISPLAY_ON"):parameter.index("portMUX_TYPE wsPendingMux")],
        "QUEUE": block_after(ws, "inline void remoteQueuePending("),
        "REPLACE": block_after(ws, "inline void remoteReplacePending("),
        "WS_REPLACE": block_after(ws, "inline void wsReplacePending("),
        "RESTORE": block_after(ws, "inline void remoteRestoreDeferredPendingLocked("),
        "FINISH_OTA": block_after(ws, "inline void remoteFinishWifiUpdateDispatch("),
        "REQUEST_DISPLAY": block_after(parameter, "inline void requestEnergyDisplay("),
        "DISPLAY_MODE": block_after(hds, "void applyEnergyDisplayMode("),
        "DISPLAY_COMMAND": block_after(hds, "void applyEnergyDisplayCommand(bool enabled) {"),
        "WAKE_HELPER": block_after(hds, "bool wakeScaleFromSoftSleep("),
        "BLE_WAKE": block_after(ble, "void softSleepOff()"),
        "WS_WAKE": block_after(sleepCommand, 'if (websocketEqualsIgnoreCase(action, "off") ||'),
        "BLE_SLEEP": block_after(ble, "void softSleepOn()"),
        "WS_SLEEP": block_after(sleepCommand, 'if (websocketEqualsIgnoreCase(action, "on"))'),
        "USB_SLEEP": block_after(usb, "void softSleepOn()"),
        "USB_WAKE": block_after(usb, "void softSleepOff()"),
        "BLE_DISPLAY_ON": block_after(ble, "void displayOn()"),
        "BLE_DISPLAY_OFF": block_after(ble, "void displayOff()"),
        "WS_DISPLAY_ON": block_after(displayCommand, 'if (websocketEqualsIgnoreCase(action, "on"))'),
        "WS_DISPLAY_OFF": block_after(displayCommand, 'if (websocketEqualsIgnoreCase(action, "off"))'),
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
    print("Soft-sleep transport/display ordering, ADC recovery and sleeping WiFi supervision passed")


if __name__ == "__main__":
    main()
