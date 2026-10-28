from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    firmware = (root / "src/hds.ino").read_text(encoding="utf-8")
    harness = r'''
#include <cassert>
#include <climits>
#include <cstdint>
#include <initializer_list>
constexpr unsigned long DEFAULT_SIGNAL_TIMEOUT_MS = 300;
struct ADS1232DebugInfo {
  int samplesInUse = 1;
  int validSamples = 1;
  bool signalTimeout = false;
  unsigned long lastConversionTimestamp = 0;
};
unsigned long now = 10000;
int refreshCount = 0, tareCount = 0, grinderNotifications = 0;
bool refreshSucceeds = true;
struct {
  ADS1232DebugInfo info;
  long bufferedRaw = 100, currentRaw = 500, tareOffset = 20;
  ADS1232DebugInfo getDebugInfo() { return info; }
  void tareNoDelay() { ++tareCount; tareOffset = bufferedRaw; }
} scale;
unsigned long millis() { return now; }
void recordEnergyActivity() {}
void grinderRuntimeNotifyTareRequested(bool) { ++grinderNotifications; }
bool refreshScaleDatasetAfterDiscontinuity(const char*) {
  ++refreshCount;
  if (!refreshSucceeds) return false;
  scale.info.validSamples = scale.info.samplesInUse;
  scale.info.lastConversionTimestamp = now;
  scale.info.signalTimeout = false;
  scale.bufferedRaw = scale.currentRaw;
  return true;
}
bool tareScaleWhenAdcReady(const char *context, bool userRequested) { @TARE@ }
void resetScale(int samples, unsigned long sampleAt) {
  scale.info = {samples, samples, false, sampleAt};
  scale.bufferedRaw = 100;
  scale.currentRaw = 500;
  scale.tareOffset = 20;
  refreshCount = tareCount = grinderNotifications = 0;
  refreshSucceeds = true;
}
void expectTare(int refreshes, long offset) {
  assert(tareScaleWhenAdcReady("test", true));
  assert(refreshCount == refreshes && tareCount == 1);
  assert(scale.tareOffset == offset);
  assert(grinderNotifications == HDS_ENABLE_GRINDER);
}
int main() {
  for (int samples : {1, 2, 4}) {
    resetScale(samples, now - 1000);
    expectTare(1, 500);
    assert(tareScaleWhenAdcReady("repeat", true));
    assert(refreshCount == 1 && tareCount == 2 && scale.tareOffset == 500);

    resetScale(samples, now - 100);
    expectTare(0, 100);

    resetScale(samples, now - DEFAULT_SIGNAL_TIMEOUT_MS);
    expectTare(0, 100);

    resetScale(samples, now - DEFAULT_SIGNAL_TIMEOUT_MS - 1);
    expectTare(1, 500);

    resetScale(samples, now);
    scale.info.validSamples = samples - 1;
    expectTare(1, 500);

    resetScale(samples, now);
    scale.info.signalTimeout = true;
    expectTare(1, 500);

    resetScale(samples, now - 1000);
    refreshSucceeds = false;
    assert(!tareScaleWhenAdcReady("failed refresh", true));
    assert(refreshCount == 1 && tareCount == 0 && scale.tareOffset == 20);
    assert(grinderNotifications == 0);
  }
  now = 100;
  resetScale(1, ULONG_MAX - 50);
  expectTare(0, 100);
  resetScale(1, ULONG_MAX - 300);
  expectTare(1, 500);
}
'''.replace("@TARE@", block_after(firmware, "bool tareScaleWhenAdcReady("))
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "tare.cpp"
        binary = Path(directory) / "tare.exe"
        source.write_text(harness, encoding="utf-8")
        for enabled in (0, 1):
            subprocess.run(["g++", "-std=c++17", f"-DHDS_ENABLE_GRINDER={enabled}",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Tare freshness, fast path, refresh failure, and timer wrap checks passed")


if __name__ == "__main__":
    main()
