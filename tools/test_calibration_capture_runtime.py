from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    menu = (root / "include/menu.h").read_text(encoding="utf-8")
    harness = r'''
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>
#include "calibration_validation.h"
struct ADS1232DebugInfo {
  long smoothedValue = 0;
  int readIndex = 0;
  uint8_t validSamples = 16;
  bool signalTimeout = false, dataOutOfRange = false;
};
struct CalibrationRawCapture { @STRUCT@ };
unsigned long now = 0;
float f_calibration_value = 1000.0f;
struct {
  std::vector<long> readings;
  std::size_t next = 0;
  unsigned long nextSampleAt = 0;
  ADS1232DebugInfo info;
  bool update() {
    if (now < nextSampleAt || next >= readings.size()) return false;
    info.smoothedValue = readings[next++];
    info.readIndex = next % 16;
    nextSampleAt = now + 100;
    return true;
  }
  ADS1232DebugInfo getDebugInfo() { return info; }
  bool getSignalTimeoutFlag() { return false; }
} scale;
unsigned long millis() { return now; }
void delay(unsigned long duration) { now += duration; }
void yield() {}
void calibrationEnsureSampleWindow() {}
long calibrationRawSpread(long firstRaw, long secondRaw) { @SPREAD@ }
void calibrationStoreRawCapture(CalibrationRawCapture &capture,
                               const ADS1232DebugInfo &firstInfo,
                               const ADS1232DebugInfo &secondInfo,
                               long spread) { @STORE@ }
bool calibrationCaptureRaw(CalibrationRawCapture &capture,
                           CalibrationRejectReason unstableReason,
                           CalibrationRejectReason &failureReason) { @CAPTURE@ }
void resetScale(std::vector<long> readings) {
  now = 0;
  scale.readings = std::move(readings);
  scale.next = scale.nextSampleAt = 0;
  scale.info = {};
}
int main() {
  for (long direction : {-1, 1}) {
    const long baseline = 1000000;
    const long plateau = baseline + direction * 800;
    std::vector<long> readings = {baseline, baseline + direction * 400, plateau};
    readings.insert(readings.end(), 30, plateau);
    resetScale(std::move(readings));
    CalibrationRawCapture capture;
    CalibrationRejectReason reason = CAL_REJECT_UNSTABLE_LOAD;
    assert(calibrationCaptureRaw(capture, CAL_REJECT_UNSTABLE_LOAD, reason));
    assert(reason == CAL_REJECT_NONE && capture.raw == plateau);
    assert(now >= 300 + CALIBRATION_STABLE_HOLD_MS);
    assert(now < CALIBRATION_CAPTURE_TIMEOUT_MS);
  }
  std::vector<long> ramp;
  for (long index = 0; index < 100; ++index) ramp.push_back(1000000 + index * 400);
  resetScale(std::move(ramp));
  CalibrationRawCapture capture;
  CalibrationRejectReason reason = CAL_REJECT_NONE;
  assert(!calibrationCaptureRaw(capture, CAL_REJECT_UNSTABLE_LOAD, reason));
  assert(reason == CAL_REJECT_UNSTABLE_LOAD && now == CALIBRATION_CAPTURE_TIMEOUT_MS);

  resetScale(std::vector<long>(30, 1000000));
  assert(calibrationCaptureRaw(capture, CAL_REJECT_UNSTABLE_ZERO, reason));
  assert(capture.raw == 1000000 && now >= 100 + CALIBRATION_STABLE_HOLD_MS);
}
'''
    replacements = {
        "STRUCT": block_after(menu, "struct CalibrationRawCapture {"),
        "SPREAD": block_after(menu, "long calibrationRawSpread("),
        "STORE": block_after(menu, "void calibrationStoreRawCapture("),
        "CAPTURE": block_after(menu, "bool calibrationCaptureRaw("),
    }
    for marker, body in replacements.items():
        harness = harness.replace(f"@{marker}@", body)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "capture.cpp"
        binary = Path(directory) / "capture.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", "-I", str(root / "include"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Calibration settling recovery, stable hold, and continuous-ramp rejection passed")


if __name__ == "__main__":
    main()
