from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    menu = (root / "include/menu.h").read_text(encoding="utf-8")
    harness = r'''
#include <cassert>
bool b_menu = true, b_calibration = false;
int i_cal_weight = 4, i_button_cal_status = 0, i_calibration = 1;
void leaveMenu() { b_menu = false; }
void calibrate() { @ENTRY@ }
int main() {
  for (int previousStage = 0; previousStage <= 3; ++previousStage) {
    b_menu = true;
    b_calibration = false;
    i_button_cal_status = previousStage;
    i_cal_weight = 4;
    calibrate();
    assert(!b_menu && b_calibration);
    assert(i_button_cal_status == 1 && i_cal_weight == 0 && i_calibration == 0);
  }
}
'''.replace("@ENTRY@", block_after(menu, "void calibrate() {"))
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "entry.cpp"
        binary = Path(directory) / "entry.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Local calibration initializes the visible first stage and default selection")


if __name__ == "__main__":
    main()
