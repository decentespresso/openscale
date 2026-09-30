from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def main():
    root = Path(__file__).resolve().parents[1]
    menu = (root / "include/menu.h").read_text(encoding="utf-8")
    hds = (root / "src/hds.ino").read_text(encoding="utf-8")
    usb = (root / "include/usbcomm.h").read_text(encoding="utf-8")
    harness = r'''
#include <cassert>
#define HDS_ENABLE_ENERGY_MENU 0
#define HDS_ENABLE_GRINDER 0
bool b_menu = true, b_calibration = false;
bool b_showChargingUI = false, b_powerOff = false;
int i_cal_weight = 4, i_button_cal_status = 0, i_calibration = 1;
int i_buttonBootDelay = 0;
const int BUTTON_SQUARE = 1;
unsigned long t_shutdownFailBle = 0, t_menuExitTime = 0;
int sampledPresses = 0, chargingWakes = 0, menuActions = 0, timerToggles = 0;
unsigned long millis() { return 10000; }
bool bleHasLiveClient() { return false; }
void recordEnergyActivity() {}
void startPressSampling(int button) { assert(button == BUTTON_SQUARE); ++sampledPresses; }
void wakeFromChargingUi(int button) {
  assert(button == BUTTON_SQUARE);
  b_showChargingUI = false;
  ++chargingWakes;
}
void leaveMenu() { b_menu = false; }
void calibrate() { @ENTRY@ }
void otherMenuAction() { ++menuActions; }
struct Menu { void (*action)(); const Menu *subMenu; const Menu *parentMenu; };
const Menu menuScale{}, menuConnections{}, menuDisplay{}, menuPower{}, menuInfo{};
const Menu menuCalibrate{calibrate, nullptr, &menuScale};
const Menu menuOther{otherMenuAction, nullptr, &menuScale};
const Menu *scaleMenu[] = {&menuCalibrate, &menuOther};
const Menu *connectionsMenu[] = {&menuConnections};
const Menu *displayMenu[] = {&menuDisplay};
const Menu *powerMenu[] = {&menuPower};
const Menu *infoMenu[] = {&menuInfo};
const Menu **currentMenu = scaleMenu;
const Menu *currentSelection = &menuCalibrate;
int currentMenuSize = 2, currentIndex = 0;
template <typename T, unsigned N> int getMenuSize(T (&)[N]) { return N; }
int connectionsMenuSize() { return 1; }
void invalidateMenuFrame() {}
void backMenu() {}
void selectMenu() { @SELECT@ }
struct SerialStub {
  void print(const char*) {}
  void println(const char*) {}
  void println(int) {}
} Serial;
void buttonSquare_Pressed() { @PRESS@ }
void toggleTimer() { ++timerToggles; }
void executeUsbSet() { @SET@ }
int main() {
  for (int previousStage = 0; previousStage <= 3; ++previousStage) {
    b_menu = true;
    b_calibration = false;
    i_button_cal_status = previousStage;
    i_cal_weight = 4;
    i_calibration = 1;
    calibrate();
    assert(!b_menu && b_calibration);
    assert(i_button_cal_status == 1 && i_cal_weight == 0 && i_calibration == 0);

    b_menu = true;
    b_calibration = false;
    i_button_cal_status = previousStage;
    i_cal_weight = 4;
    i_calibration = 1;
    const int beforePress = sampledPresses;
    executeUsbSet();
    assert(!b_menu && b_calibration);
    assert(i_button_cal_status == 1 && i_cal_weight == 0 && i_calibration == 0);
    assert(sampledPresses == beforePress + 1);
  }

  b_menu = true;
  b_calibration = false;
  i_button_cal_status = 3;
  selectMenu();
  assert(!b_menu && b_calibration && i_button_cal_status == 1);

  executeUsbSet();
  assert(!b_menu && b_calibration && i_button_cal_status == 2);
  buttonSquare_Pressed();
  assert(!b_menu && b_calibration && i_button_cal_status == 3);

  b_menu = true;
  b_calibration = false;
  currentSelection = &menuOther;
  const int beforeMenuPress = sampledPresses;
  executeUsbSet();
  assert(b_menu && !b_calibration && menuActions == 1);
  assert(i_button_cal_status == 3 && sampledPresses == beforeMenuPress + 1);

  b_menu = false;
  const int beforeTimerPress = sampledPresses;
  executeUsbSet();
  assert(timerToggles == 1 && sampledPresses == beforeTimerPress);

  b_showChargingUI = true;
  executeUsbSet();
  assert(!b_showChargingUI && chargingWakes == 1 && timerToggles == 1);
  assert(sampledPresses == beforeTimerPress + 1 && i_button_cal_status == 3);
}
'''
    replacements = {
        "ENTRY": block_after(menu, "void calibrate() {"),
        "SELECT": block_after(menu, "void selectMenu() {"),
        "PRESS": block_after(hds, "void buttonSquare_Pressed() {"),
        "SET": block_after(usb, 'if (inputString.startsWith("set"))'),
    }
    for marker, body in replacements.items():
        harness = harness.replace(f"@{marker}@", body)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "entry.cpp"
        binary = Path(directory) / "entry.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Calibration entry, USB menu selection, step confirmation, and charging wake checks passed")


if __name__ == "__main__":
    main()
