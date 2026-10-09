from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    firmware = (root / "src/hds.ino").read_text(encoding="utf-8")
    runtime = (root / "include/grinder_runtime.h").read_text(encoding="utf-8")
    usb = (root / "include/usbcomm.h").read_text(encoding="utf-8")
    recovery = block_after(firmware, "else if (scale.getSignalTimeoutFlag() &&")
    harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
enum State { GRINDER_STATE_DISABLED, GRINDER_STATE_ERROR, GRINDER_STATE_GRINDING,
  GRINDER_STATE_STOPPING, GRINDER_STATE_ARMED, GRINDER_STATE_CONNECTED };
struct { bool enabled = true; } grinderSettings;
bool connected = true, offSucceeds = true, refreshSucceeds = true;
bool adcAlive = true, tareSucceeds = true;
int order = 0, offAt = 0, closeAt = 0, powerAt = 0, tareAt = 0;
int responseMode = -1, responseStatus = -1, responseCount = 0;
unsigned long now = 10000;
constexpr int LOW = 0;
struct Client { bool connected() { return ::connected; } };
struct {
  Client client;
  State state = GRINDER_STATE_GRINDING;
  bool resumeAfterRecovery = true;
  int recovery = 1;
} grinderRuntime;
bool b_adc_recovery_active = false;
unsigned int i_adc_recovery_count = 0;
unsigned long t_lastScaleData = 1, t_lastScaleRecovery = 0;
struct {
  template<class T> void print(T) {}
  template<class T> void println(T) {}
  void printf(const char*, const char*) {}
} Serial;
bool grinderSendOff() { offAt = ++order; return offSucceeds; }
int grinderRecoveryComplete() { return 0; }
void grinderSetStatus(const char*) {}
void grinderCloseClient() { closeAt = ++order; connected = false; }
void grinderSetState(State state) { grinderRuntime.state = state; }
void grinderEnterError(const char *status) { @ERROR@ }
struct {
  void powerDown() {
    powerAt = ++order;
#if HDS_ENABLE_GRINDER
    if (grinderSettings.enabled && grinderRuntime.state != GRINDER_STATE_DISABLED) {
      assert(grinderRuntime.state == GRINDER_STATE_ERROR && !connected);
      assert(!grinderRuntime.resumeAfterRecovery);
    }
#endif
  }
  void powerUp() {}
  int getDoutPin() { return 1; }
} scale;
void delay(unsigned long duration) { now += duration; }
unsigned long millis() { return now; }
void yield() { ++now; }
int digitalRead(int) { return adcAlive ? LOW : 1; }
bool refreshScaleDatasetAfterDiscontinuity(const char*) { return refreshSucceeds; }
bool tareScaleWhenAdcReady(const char*) { tareAt = ++order; return tareSucceeds; }
void resetScaleOutputAfterAdcDiscontinuity() {}
void recover() { @RECOVER@ }
void sendUsbAdsResetResponse(uint8_t mode, uint8_t status) {
  responseMode = mode;
  responseStatus = status;
  ++responseCount;
}
void handleAdsReset(uint8_t mode) { @USB_RESET@ }
int main() {
  for (State state : {GRINDER_STATE_GRINDING, GRINDER_STATE_STOPPING,
                      GRINDER_STATE_ARMED, GRINDER_STATE_CONNECTED}) {
    for (const bool offSuccess : {false, true}) {
      for (const bool refreshSuccess : {false, true}) {
        grinderRuntime.state = state;
        grinderRuntime.resumeAfterRecovery = true;
        connected = true;
        offSucceeds = offSuccess;
        refreshSucceeds = refreshSuccess;
        order = offAt = closeAt = powerAt = tareAt = 0;
        recover();
#if HDS_ENABLE_GRINDER
        assert(offAt > 0 && offAt < closeAt && closeAt < powerAt);
        assert(grinderRuntime.state == GRINDER_STATE_ERROR);
        assert(!grinderRuntime.resumeAfterRecovery);
#else
        assert(offAt == 0 && closeAt == 0);
#endif
        assert(refreshSuccess ? tareAt > powerAt : tareAt == 0);
      }
    }
  }
  for (State state : {GRINDER_STATE_GRINDING, GRINDER_STATE_STOPPING,
                      GRINDER_STATE_ARMED, GRINDER_STATE_CONNECTED,
                      GRINDER_STATE_ERROR, GRINDER_STATE_DISABLED}) {
    for (uint8_t mode : {0, 1, 2}) {
      for (int failure = 0; failure < 4; ++failure) {
        for (bool success : {false, true}) {
          const bool active = state != GRINDER_STATE_ERROR && state != GRINDER_STATE_DISABLED;
          grinderRuntime.state = state;
          grinderRuntime.resumeAfterRecovery = active;
          connected = active;
          offSucceeds = success;
          adcAlive = failure != 1;
          refreshSucceeds = failure != 2;
          tareSucceeds = failure != 3;
          order = offAt = closeAt = powerAt = tareAt = responseCount = 0;
          responseMode = responseStatus = -1;
          handleAdsReset(mode);
#if HDS_ENABLE_GRINDER
          if (active) {
            assert(offAt > 0 && offAt < closeAt && closeAt < powerAt);
            assert(grinderRuntime.state == GRINDER_STATE_ERROR);
            assert(!grinderRuntime.resumeAfterRecovery);
          } else {
            assert(offAt == 0 && closeAt == 0 && grinderRuntime.state == state);
          }
#else
          assert(offAt == 0 && closeAt == 0 && grinderRuntime.state == state);
#endif
          assert(powerAt > 0);
          assert(responseCount == 1 && responseMode == mode);
          assert(responseStatus == (failure == 1 ? 1 :
                 failure == 2 || (failure == 3 && mode == 2) ? 2 : 0));
          assert(mode == 2 && adcAlive && refreshSucceeds ? tareAt > powerAt : tareAt == 0);
        }
      }
    }
  }
  grinderSettings.enabled = false;
  order = offAt = closeAt = powerAt = 0;
  recover();
  assert(offAt == 0 && closeAt == 0 && powerAt > 0);
  for (uint8_t mode : {0, 1, 2}) {
    order = offAt = closeAt = powerAt = 0;
    handleAdsReset(mode);
    assert(offAt == 0 && closeAt == 0 && powerAt > 0);
  }
  std::puts("ADC recovery and USB reset request OFF and latch error before ADC work");
}
'''.replace("@ERROR@", block_after(runtime, "void grinderEnterError("))
    harness = harness.replace("@RECOVER@", recovery)
    harness = harness.replace("@USB_RESET@", block_after(usb, "void handleAdsReset(uint8_t mode) {"))
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "recovery.cpp"
        binary = Path(directory) / "recovery.exe"
        source.write_text("#include <initializer_list>\n" + harness, encoding="utf-8")
        for enabled in (0, 1):
            subprocess.run(["g++", "-std=c++17", f"-DHDS_ENABLE_GRINDER={enabled}",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
