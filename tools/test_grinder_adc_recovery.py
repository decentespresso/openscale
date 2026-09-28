from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    firmware = (root / "src/hds.ino").read_text(encoding="utf-8")
    runtime = (root / "include/grinder_runtime.h").read_text(encoding="utf-8")
    recovery = block_after(firmware, "else if (scale.getSignalTimeoutFlag() &&")
    harness = r'''
#include <cassert>
#include <cstdio>
enum State { GRINDER_STATE_DISABLED, GRINDER_STATE_ERROR, GRINDER_STATE_GRINDING,
  GRINDER_STATE_STOPPING, GRINDER_STATE_ARMED, GRINDER_STATE_CONNECTED };
struct { bool enabled = true; } grinderSettings;
bool connected = true, offSucceeds = true, refreshSucceeds = true;
int order = 0, offAt = 0, closeAt = 0, powerAt = 0, tareAt = 0;
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
  void println(const char*) {}
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
} scale;
void delay(unsigned long) {}
unsigned long millis() { return 10000; }
bool refreshScaleDatasetAfterDiscontinuity(const char*) { return refreshSucceeds; }
bool tareScaleWhenAdcReady(const char*) { tareAt = ++order; return true; }
void resetScaleOutputAfterAdcDiscontinuity() {}
void recover() { @RECOVER@ }
int main() {
  for (State state : {GRINDER_STATE_GRINDING, GRINDER_STATE_STOPPING,
                      GRINDER_STATE_ARMED, GRINDER_STATE_CONNECTED}) {
    for (bool success : {false, true}) {
      grinderRuntime.state = state;
      grinderRuntime.resumeAfterRecovery = true;
      connected = true;
      offSucceeds = refreshSucceeds = success;
      order = offAt = closeAt = powerAt = tareAt = 0;
      recover();
#if HDS_ENABLE_GRINDER
      assert(offAt > 0 && offAt < closeAt && closeAt < powerAt);
      assert(grinderRuntime.state == GRINDER_STATE_ERROR);
      assert(!grinderRuntime.resumeAfterRecovery);
#else
      assert(offAt == 0 && closeAt == 0);
#endif
      assert(success ? tareAt > powerAt : tareAt == 0);
    }
  }
  grinderSettings.enabled = false;
  order = offAt = closeAt = powerAt = 0;
  recover();
  assert(offAt == 0 && closeAt == 0 && powerAt > 0);
  std::puts("ADC recovery requests OFF and latches error before blocking recovery");
}
'''.replace("@ERROR@", block_after(runtime, "void grinderEnterError("))
    harness = harness.replace("@RECOVER@", recovery)
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
