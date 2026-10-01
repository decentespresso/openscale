from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


def run_check(hds, menu):
    loop = block_after(hds, "void loop() {")
    charging_start = loop.index("else if (GPIO_power_on_with == BATTERY_CHARGING")
    charging_opening = loop.index("{", charging_start)
    charging_guard = loop[charging_start + len("else "):charging_opening]
    harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#define HDS_ENABLE_ENERGY_MENU 0
#define HDS_ENABLE_GRINDER 0
#define USB_DET 4
constexpr int LOW = 0, HIGH = 1, BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2, BATTERY_CHARGING = 3;
bool b_menu, b_calibration, b_showChargingUI, b_powerOff, b_is_charging;
bool b_u8g2Sleep = false, b_softSleep = false, b_websocketLowPowerEnabled = false;
bool b_menuCirclePressPending = false, b_menuSquarePressPending = false;
bool b_menuCircleLongHandled = false, b_menuSquareLongHandled = false;
bool b_chargingOLED = true, bleClientLive = false;
int i_cal_weight = 0, i_button_cal_status = 1, i_calibration = 0, i_buttonBootDelay = 0;
int GPIO_power_on_with = BATTERY_CHARGING, chargingPin = LOW, usbPin = LOW;
int sampledPresses = 0, releasedPresses = 0, timerToggles = 0, chargingWakes = 0;
int calibrationFrames = 0, chargingFrames = 0, adcWakes = 0, adcResets = 0;
unsigned long t_shutdownFailBle = 9000, t_menuExitTime = 0;
float f_batteryVoltage = 4.2f, showEmptyBatteryBelowVoltage = 3.2f, showFullBatteryAboveVoltage = 4.2f;
unsigned long millis() { return 10000; }
bool bleHasLiveClient() { return bleClientLive; }
int digitalRead(int pin) { return pin == USB_DET ? usbPin : chargingPin; }
long map(long, long, long, long, long) { return 50; }
void recordEnergyActivity() {}
void power_off(int) {}
void wakeScaleFromSoftSleep(const char*) {}
void wakeFromChargingUi(int pin) {
  ++chargingWakes;
  GPIO_power_on_with = pin;
  b_showChargingUI = b_is_charging = false;
}
void startPressSampling(int pin) { assert(pin == BUTTON_SQUARE); ++sampledPresses; }
void onButtonReleased(int pin) { assert(pin == BUTTON_SQUARE); ++releasedPresses; ++timerToggles; }
void leaveMenu() { b_menu = false; }
void invalidateMenuFrame() {}
void navigateMenu(int) {}
void backMenu() {}
void buttonCircle_Pressed() {}
void buttonCircle_Released() {}
void buttonCircle_DoubleClicked() {}
void buttonSquare_DoubleClicked() {}
void buttonCircle_LongPressed() {}
void buttonSquare_LongPressed() {}
void chargingOLED(int, float) { ++chargingFrames; }
bool refreshScaleDatasetAfterDiscontinuity(const char*) { return true; }
bool tareScaleWhenAdcReady(const char*) { return true; }
void resetScaleOutputAfterAdcDiscontinuity() { ++adcResets; }
void calibration(int input) { assert(input == i_calibration); ++calibrationFrames; }
struct { void powerUp() { ++adcWakes; } } scale;
struct {
  void setPowerSave(int) {}
  void setContrast(int) {}
} u8g2;
struct {
  void print(const char*) {}
  void println(const char*) {}
  void println(int) {}
} Serial;
struct AceButton {
  enum : uint8_t { kEventPressed, kEventDoubleClicked, kEventLongPressed, kEventReleased };
  int getPin() const { return BUTTON_SQUARE; }
} buttonSquare;
void calibrate() { @ENTRY@ }
struct Menu { void (*action)(); const Menu *subMenu; const Menu *parentMenu; };
const Menu menuScale{}, menuConnections{}, menuDisplay{}, menuPower{}, menuInfo{};
const Menu menuCalibrate{calibrate, nullptr, &menuScale};
const Menu *scaleMenu[] = {&menuCalibrate};
const Menu *connectionsMenu[] = {&menuConnections};
const Menu *displayMenu[] = {&menuDisplay};
const Menu *powerMenu[] = {&menuPower};
const Menu *infoMenu[] = {&menuInfo};
const Menu **currentMenu = scaleMenu;
const Menu *currentSelection = &menuCalibrate;
int currentMenuSize = 1, currentIndex = 0;
template <typename T, unsigned N> int getMenuSize(T (&)[N]) { return N; }
int connectionsMenuSize() { return 1; }
void selectMenu() { @SELECT@ }
void buttonSquare_Pressed() { @PRESS@ }
void buttonSquare_Released() { @RELEASE@ }
void aceButtonHandleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) { @EVENT@ }
void serviceUi() {
  if (b_menu) return;
  @CHARGING_GUARD@ { @CHARGING@ }
  else if (b_calibration == true) { @CALIBRATION@ }
}
void squareEvent(uint8_t event) { aceButtonHandleEvent(&buttonSquare, event, HIGH); }
void resetRuntime(bool ble) {
  bleClientLive = ble;
  b_menu = b_calibration = b_showChargingUI = b_powerOff = false;
  b_is_charging = true;
  GPIO_power_on_with = BATTERY_CHARGING;
  chargingPin = usbPin = LOW;
  i_button_cal_status = 1;
  sampledPresses = releasedPresses = timerToggles = 0;
  chargingWakes = 0;
  calibrationFrames = chargingFrames = adcWakes = adcResets = 0;
}
void testPhysicalCalibrationEntry(bool ble) {
  resetRuntime(ble);
  b_menu = true;
  squareEvent(AceButton::kEventPressed);
  assert(b_menu && !b_calibration);
  squareEvent(AceButton::kEventReleased);
  assert(!b_menu && b_calibration && !b_powerOff);
  assert(!b_menuSquarePressPending);
  serviceUi();
  assert(calibrationFrames == 1 && chargingFrames == 0 && !b_powerOff);
  assert(GPIO_power_on_with == BATTERY_CHARGING && b_is_charging);
}
void testCalibrationConfirmation(bool ble, bool chargingUi) {
  resetRuntime(ble);
  b_calibration = true;
  b_showChargingUI = chargingUi;
  squareEvent(AceButton::kEventPressed);
  squareEvent(AceButton::kEventReleased);
  assert(i_button_cal_status == 2 && b_calibration && !b_powerOff);
  assert(sampledPresses == 0 && releasedPresses == 0 && timerToggles == 0);
  assert(chargingWakes == 0 && b_showChargingUI == chargingUi);
  assert(GPIO_power_on_with == BATTERY_CHARGING && b_is_charging);
}
void testChargingAndWeighing() {
  resetRuntime(false);
  serviceUi();
  assert(chargingFrames == 1 && b_showChargingUI && !b_powerOff);
  assert(calibrationFrames == 0 && adcWakes == 0);
  for (float voltage : {4.0f, 4.2f}) {
    resetRuntime(false);
    chargingPin = HIGH;
    f_batteryVoltage = voltage;
    serviceUi();
    assert(b_powerOff && !b_showChargingUI && calibrationFrames == 0);
  }
  resetRuntime(false);
  chargingPin = usbPin = HIGH;
  serviceUi();
  assert(!b_powerOff && !b_showChargingUI && !b_is_charging);
  assert(GPIO_power_on_with == BUTTON_SQUARE && adcWakes == 1 && adcResets == 1);
  squareEvent(AceButton::kEventPressed);
  squareEvent(AceButton::kEventReleased);
  assert(sampledPresses == 1 && releasedPresses == 1 && timerToggles == 1);
  resetRuntime(false);
  b_showChargingUI = true;
  squareEvent(AceButton::kEventPressed);
  assert(chargingWakes == 1 && !b_showChargingUI && !b_is_charging);
  assert(GPIO_power_on_with == BUTTON_SQUARE && sampledPresses == 1);
  resetRuntime(true);
  squareEvent(AceButton::kEventPressed);
  assert(b_powerOff && sampledPresses == 1);
}
int main() {
  for (bool ble : {false, true}) {
    testPhysicalCalibrationEntry(ble);
    for (bool chargingUi : {false, true}) testCalibrationConfirmation(ble, chargingUi);
  }
  testChargingAndWeighing();
}
'''
    replacements = {
        "ENTRY": block_after(menu, "void calibrate() {"),
        "SELECT": block_after(menu, "void selectMenu() {"),
        "PRESS": block_after(hds, "void buttonSquare_Pressed() {"),
        "RELEASE": block_after(hds, "void buttonSquare_Released() {"),
        "EVENT": block_after(hds, "void aceButtonHandleEvent("),
        "CHARGING_GUARD": charging_guard,
        "CHARGING": block_after(loop, "else if (GPIO_power_on_with == BATTERY_CHARGING"),
        "CALIBRATION": block_after(loop, "if (b_calibration == true)"),
    }
    for marker, body in replacements.items():
        harness = harness.replace(f"@{marker}@", body)
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "runtime.cpp"
        binary = Path(directory) / "runtime.exe"
        source.write_text(harness, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Physical calibration entry, Square isolation, and charging runtime checks passed")


def main():
    root = Path(__file__).resolve().parents[1]
    run_check(
        (root / "src/hds.ino").read_text(encoding="utf-8"),
        (root / "include/menu.h").read_text(encoding="utf-8"),
    )


if __name__ == "__main__":
    main()
