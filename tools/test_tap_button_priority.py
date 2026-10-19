from pathlib import Path
import shutil
import subprocess
import tempfile

from test_tap_action_contract import function_body


ROOT = Path(__file__).resolve().parents[1]


def main():
    tap = (ROOT / "include/tap_detection.h").read_text(encoding="utf-8")
    source = r'''
#include <cassert>
#include <cstdint>
const int LOW = 0, HIGH = 1, BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2;
const unsigned long DOUBLECLICK_DELAY = 600;
unsigned long now = 10000, t_menuExitTime = 0, t_shutdownFailBle = 0;
unsigned long t_quickZeroStart = 0, t_tareByButton = 0;
float f_current_raw_value = 0;
bool b_tapTareEnabled = true, b_tapTimerEnabled = true;
bool b_shutdownFailBle = false, b_powerOff = false;
bool b_bootTare = false, b_bootFreshTarePending = false;
bool b_weight_quick_zero = false, b_tareByButton = false;
bool tapDetectionGated = false;
int circle = HIGH, square = HIGH, timerActions = 0;
struct Press { bool active = false; unsigned long releaseTime = 0; } circle_press_data, square_press_data;
struct Timer { bool isRunning() { return false; } } stopWatch;
enum class TapEvent { Double, Triple };
TapEvent nextEvent = TapEvent::Double;
struct Detector {
  int ticks = 0, resets = 0;
  void reset(unsigned long, float) { resets++; }
  TapEvent tick(unsigned long, float) { ticks++; return nextEvent; }
} tapDetector;
struct Logger { void println(const char *) {};} Serial;
int digitalRead(int pin) { return pin == BUTTON_CIRCLE ? circle : square; }
unsigned long millis() { return now; }
void power_off(int) {}
void scaleTimer() { timerActions++; }
'''
    for declaration, name in (
        ("void runTapLocalAction(bool tripleTap)", "runTapLocalAction"),
        ("void tapDetectTick()", "tapDetectTick"),
    ):
        source += declaration + function_body(tap, name) + "\n"
    source += r'''
void blocked() {
  const int ticks = tapDetector.ticks;
  b_tareByButton = false;
  tapDetectTick();
  assert(tapDetector.ticks == ticks && !b_tareByButton && tapDetectionGated);
}
int main() {
  tapDetectTick(); assert(b_tareByButton);
  circle = LOW; blocked(); circle = HIGH;
  square = LOW; blocked(); square = HIGH;
  circle_press_data.active = true; blocked(); circle_press_data.active = false;
  square_press_data.active = true; blocked(); square_press_data.active = false;
  for (auto *press : {&circle_press_data, &square_press_data}) {
    press->releaseTime = now - DOUBLECLICK_DELAY; blocked(); press->releaseTime = 0;
  }
  b_shutdownFailBle = true; t_shutdownFailBle = now - 2999; blocked();
  b_shutdownFailBle = false;
  b_powerOff = true; blocked(); assert(b_powerOff); b_powerOff = false;
  const int resets = tapDetector.resets;
  tapDetectTick(); assert(b_tareByButton && !tapDetectionGated && tapDetector.resets > resets);
  nextEvent = TapEvent::Triple;
  square = LOW; blocked(); assert(timerActions == 0); square = HIGH;
  tapDetectTick(); assert(timerActions == 1);
}
'''
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        cpp, binary = path / "tap.cpp", path / "tap.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-include", "initializer_list", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("tap button priority runtime tests passed")


if __name__ == "__main__":
    main()
