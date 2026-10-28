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
    timerCommand = ws[ws.index('if (websocketEqualsIgnoreCase(command, "timer"))'):]
    loop = block_after(hds, "void loop()")
    sleepGate = loop.index("if (!b_softSleep)")
    wifiGuard = "if (b_softSleep && b_wifiEnabled)"
    assert loop.index(wifiGuard) < sleepGate
    assert loop.index("if (b_ota)") < loop.index(wifiGuard)
    harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include "energy_runtime_policy.h"
using std::isfinite;
template<typename L, typename R> auto max(L left, R right) { return left > right ? left : right; }
@COMMANDS@
bool b_softSleep = false, b_u8g2Sleep = false, b_wifiEnabled = true;
bool chargingUi = false;
bool chargingUiActive() { return chargingUi; }
bool b_ota = false, b_powerOff = false, pendingOtaDispatching = false;
bool b_hasFuelGauge = false;
bool fuelGaugeLegacyHardware() { return !b_hasFuelGauge; }
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
struct {
  unsigned long elapsedMs = 42000;
  bool running = false;
  int calls = 0;
  void reset() {
    assert(wsPendingMux == 0 && !b_ota);
    elapsedMs = 0;
    running = false;
    ++calls;
  }
  void start() {
    assert(wsPendingMux == 0 && !b_ota);
    running = true;
    ++calls;
  }
  void stop() {
    assert(wsPendingMux == 0 && !b_ota);
    running = false;
    ++calls;
  }
  bool isRunning() { return running; }
} stopWatch;
struct {
  int getSamplesInUse() { return 1; }
  bool getSignalTimeoutFlag() { return false; }
  float getConversionTime() { return 100; }
  void powerUp() { assert(wsPendingMux == 0); ++wakes; }
} scale;
struct {
  bool explicitDisplayOff = false;
  DisplayIdleMode displayMode = DisplayIdleMode::Active;
  int requestedDisplayContrast = 255;
  struct { void invalidate() {} } oledFrames;
} energyRuntime;
struct { void recordActivity(unsigned long) {} } energyPolicy;
unsigned long clockMs = 1;
unsigned long millis() { return clockMs; }
void delay(int) { assert(wsPendingMux == 0); }
void reset() { assert(false); }
bool setScaleSamplesInUseWhenReady(uint8_t, const char*) { return true; }
void applyEnergyLowPowerCommand() {}
void recordEnergyActivity() {}
void clearPendingEnergyActivity() {}
int taskNotifications = 0;
void notifyEnergyMainLoop() { ++taskNotifications; }
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
int statusReplies = 0;
void sendWebsocketStatus(void*, const char*) { ++statusReplies; }
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
void bleTimerCommand(int action) {
  if (action == 0) { @BLE_TIMER_START@ }
  else if (action == 1) { @BLE_TIMER_STOP@ }
  else { @BLE_TIMER_ZERO@ }
}
bool wsTimerCommand(int action) {
  void *client = nullptr;
  if (action == 0) { @WS_TIMER_START@ }
  if (action == 1) { @WS_TIMER_STOP@ }
  @WS_TIMER_ZERO@
}
void queueTimerCommand(int source, int action) {
  if (source == 0) bleTimerCommand(action);
  else assert(wsTimerCommand(action));
}
void dispatch() { @DISPATCH@ }
int deferredTimerSource = 0, deferredTimerAction = 0;
void queueTimerBeforeOtaPause() {
  queueTimerCommand(deferredTimerSource, deferredTimerAction);
  b_ota = true;
}
void testTimerCommands() {
  struct TimerCase { int first, last; unsigned long elapsedMs; bool running; };
  const TimerCase cases[] = {
    {0, 0, 0, true}, {0, 1, 0, false}, {0, 2, 0, false},
    {1, 0, 0, true}, {1, 1, 42000, false}, {1, 2, 0, false},
    {2, 0, 0, true}, {2, 1, 0, false}, {2, 2, 0, false},
  };
  for (const auto &test : cases) {
    for (const bool initiallyRunning : {false, true}) {
      for (const int firstSource : {0, 1}) {
        for (const int lastSource : {0, 1}) {
          for (const bool deferred : {false, true}) {
            stopWatch.elapsedMs = 42000;
            stopWatch.running = initiallyRunning;
            stopWatch.calls = 0;
            const int repliesBefore = statusReplies;
            queueTimerCommand(firstSource, test.first);
            if (deferred) {
              deferredTimerSource = lastSource;
              deferredTimerAction = test.last;
              afterSnapshot = queueTimerBeforeOtaPause;
              dispatch();
              assert(!afterSnapshot && b_ota && stopWatch.calls == 0);
              const auto pending = wsPendingMask;
              dispatch();
              assert(wsPendingMask == pending && stopWatch.calls == 0);
              b_ota = false;
            } else {
              queueTimerCommand(lastSource, test.last);
            }
            assert(statusReplies == repliesBefore + (firstSource == 1) + (lastSource == 1));
            assert(stopWatch.calls == 0 && stopWatch.elapsedMs == 42000);
            assert(stopWatch.running == initiallyRunning);
            dispatch();
            assert(stopWatch.elapsedMs == test.elapsedMs && stopWatch.running == test.running);
            assert(wsPendingMask == 0);
          }
        }
      }
    }
  }
}
#if HDS_ENABLE_ENERGY_MENU
int remoteTareMux = 0;
uint8_t i_remoteTareRequests = 0;
bool b_tareByBle = false;
unsigned long t_tareByBle = 0;
unsigned long t_batteryRefresh = 0;
unsigned long i_batteryRefreshTareInterval = 30000;
unsigned long t_firstConnect = 0, t_heartBeat = 0, t_lastDisconnectAttempt = 0;
bool b_weight_in_serial = false, b_usbweight_enabled = false, b_requireHeartBeat = true;
unsigned int bleStatusResponsesPending = 0, bleVoltageResponsesPending = 0;
unsigned int otaDisplayState = 0;
constexpr unsigned int OTA_DISPLAY_NONE = 0;
constexpr int SCALE_DOUT = 11;
int digitalRead(int) { return HIGH; }
constexpr unsigned long WEIGHT_BASE_INTERVAL_MS = 100;
constexpr unsigned long ENERGY_ADC_RECOVERY_START_MS = 1501;
constexpr unsigned long ENERGY_ADC_RECOVERY_RETRY_MS = 5001;
constexpr unsigned long ENERGY_ADC_SIGNAL_TIMEOUT_MS = 301;
constexpr unsigned long ADS1232_DEFAULT_CONVERSION_MS = 100;
constexpr unsigned long BUTTON_POLL_INTERVAL_MS = 2;
constexpr unsigned long ENERGY_USB_FALLBACK_POLL_MS = 10;
constexpr unsigned long ENERGY_DISPLAY_DEADLINE_MS = 500;
constexpr unsigned long WIFI_SUPERVISE_INTERVAL_MS = 250;
constexpr unsigned long HEARTBEAT_TIMEOUT = 5000;
constexpr unsigned long HEARTBEAT_DISCONNECT_RETRY_INTERVAL = 10000;
struct {
  unsigned long lastScaleData = 0, lastScaleRecovery = 0;
  unsigned long lastButtonPoll = 0, lastWeightTick = 0;
  bool scaleWakeFallback = false, circleButtonWakeFallback = false;
  bool squareButtonWakeFallback = false, usbWakeFallback = false;
  bool buttonGestureActive = false;
} energyIdle;
bool anyScaleButtonPressed() { return false; }
bool bleHasLiveClient() { return false; }
bool energyMainLoopCanBlock() { return true; }
using TickType_t = unsigned long;
constexpr bool pdTRUE = true;
TickType_t pdMS_TO_TICKS(unsigned long ms) { return ms; }
int waits = 0;
TickType_t lastWaitTicks = 0;
void ulTaskNotifyTake(bool, TickType_t ticks) { ++waits; lastWaitTicks = ticks; }
void requestRemoteTare() { @REQUEST_TARE@ }
bool hasRemoteTareRequest() { @HAS_TARE@ }
uint8_t consumeRemoteTareRequests() { @CONSUME_TARE@ }
bool energyMainLoopHasPendingWork() { @PENDING_WORK@ }
unsigned long energyMainLoopWaitMs(unsigned long now) { @WAIT_MS@ }
void waitForEnergyMainLoopWork() { @WAIT_WORK@ }
#endif
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
  testTimerCommands();
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
  b_hasFuelGauge = true;
  for (transport = 0; transport < 3; ++transport) {
    for (const bool gpout : {false, true}) {
      accessory = gpout;
      sleepCommand();
      dispatch();
      assert(b_softSleep && !rail && accessory == gpout);
      wakeCommand();
      dispatch();
      assert(!b_softSleep && rail && accessory == gpout);
    }
  }
  b_softSleep = true;
  supervise();
  assert(supervises == 1);
#if HDS_ENABLE_ENERGY_MENU
  sleepCommand();
  dispatch();
  t_batteryRefresh = 1000;
  for (const unsigned long now : {31001UL, 61001UL}) {
    clockMs = now;
    assert(!energyMainLoopHasPendingWork());
    assert(energyMainLoopWaitMs(now) == 200);
    const int waitsBefore = waits;
    waitForEnergyMainLoopWork();
    assert(waits == waitsBefore + 1 && lastWaitTicks == 200);
    assert(t_batteryRefresh == 1000);
  }
  clockMs = 2000;
  const int notificationsBefore = taskNotifications;
  requestRemoteTare();
  assert(taskNotifications == notificationsBefore + 1);
  assert(hasRemoteTareRequest() && !energyMainLoopHasPendingWork());
  const int waitsBefore = waits;
  waitForEnergyMainLoopWork();
  assert(waits == waitsBefore + 1 && i_remoteTareRequests == 1);
  remoteQueuePending(WSP_TIMER_START);
  waitForEnergyMainLoopWork();
  assert(waits == waitsBefore + 1);
  dispatch();
  wakeCommand();
  dispatch();
  assert(!b_softSleep && energyMainLoopHasPendingWork());
  assert(consumeRemoteTareRequests() == 1 && !hasRemoteTareRequest());
  clockMs = 31001;
  assert(energyMainLoopWaitMs(clockMs) == 0);
#endif
  b_wifiEnabled = false;
  supervise();
  assert(supervises == 1);
  b_wifiEnabled = true;
  b_softSleep = false;
  supervise();
  assert(supervises == 1);
  for (bool charging : {false, true}) {
    for (bool asleep : {false, true}) {
      for (uint32_t explicitDisplay : {0u, WSP_DISPLAY_ON, WSP_DISPLAY_OFF}) {
        chargingUi = charging;
        b_softSleep = asleep;
        oled = false;
        b_u8g2Sleep = true;
        wsPendingMask = WSP_DISPLAY_RESTORE | explicitDisplay;
        dispatch();
        const bool expected = explicitDisplay == WSP_DISPLAY_ON ||
          (explicitDisplay == 0 && !charging && !asleep);
        assert(oled == expected && b_u8g2Sleep != expected && wsPendingMask == 0);
      }
    }
  }
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
        "BLE_TIMER_START": block_after(ble, "void timerStart()"),
        "BLE_TIMER_STOP": block_after(ble, "void timerStop()"),
        "BLE_TIMER_ZERO": block_after(ble, "void timerZero()"),
        "WS_TIMER_START": block_after(timerCommand, 'if (websocketEqualsIgnoreCase(action, "start"))'),
        "WS_TIMER_STOP": block_after(timerCommand, 'if (websocketEqualsIgnoreCase(action, "stop"))'),
        "WS_TIMER_ZERO": block_after(timerCommand, 'if (websocketEqualsIgnoreCase(action, "zero") ||'),
        "DISPATCH": block_after(ws, "void processWsPendingCmds()"),
        "WIFI": block_after(loop, wifiGuard),
        "REQUEST_TARE": block_after(parameter, "void requestRemoteTare()"),
        "HAS_TARE": block_after(parameter, "bool hasRemoteTareRequest()"),
        "CONSUME_TARE": block_after(parameter, "uint8_t consumeRemoteTareRequests()"),
        "PENDING_WORK": block_after(hds, "bool energyMainLoopHasPendingWork()"),
        "WAIT_MS": block_after(hds, "unsigned long energyMainLoopWaitMs("),
        "WAIT_WORK": block_after(hds, "void waitForEnergyMainLoopWork() {"),
    }
    for marker, source in replacements.items():
        harness = harness.replace(f"@{marker}@", source)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "check.cpp"
        source.write_text(harness, encoding="utf-8")
        for energy, webserver in ((0, 0), (0, 1), (1, 0), (1, 1)):
            binary = Path(directory) / "check.exe"
            subprocess.run(["g++", "-std=c++17", "-I", str(ROOT / "include"),
                            f"-DHDS_ENABLE_ENERGY_MENU={energy}",
                            f"-DHDS_FEATURE_WEBSERVER={webserver}", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Remote timer composition, soft-sleep ordering, ADC recovery, WiFi supervision and idle scheduling passed")


if __name__ == "__main__":
    main()
