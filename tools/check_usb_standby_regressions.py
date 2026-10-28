import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--acebutton", type=Path, default=ROOT / ".pio.nosync/libdeps/esp32s3-energy-menu/AceButton")
    library = parser.parse_args().acebutton.resolve()
    assert "version=1.10.1" in (library / "library.properties").read_text()
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    hds = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    parameter = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    usb = (ROOT / "include/usbcomm.h").read_text(encoding="utf-8")
    ble = (ROOT / "include/ble.h").read_text(encoding="utf-8")
    ws = (ROOT / "include/websocket.h").read_text(encoding="utf-8")
    names = ["anyScaleButtonPressed", "resetReleasedScaleButtons", "buttonChecksSuppressedUntilRelease",
             "serviceScaleButtonInputs", "enterUsbStandby", "wakeFromChargingUi", "handleMenuChord",
             "button_init", "aceButtonHandleEvent", "buttonCircle_Pressed", "buttonSquare_Pressed",
             "buttonCircle_Released", "buttonSquare_Released", "buttonCircle_LongPressed",
             "buttonSquare_LongPressed", "buttonCircle_DoubleClicked", "buttonSquare_DoubleClicked"]
    signatures = [re.search(r"^(?:bool|void) " + name + r"\([^;]*?\) \{", hds, re.M).group(0)[:-2]
                  for name in names]
    source = r'''
#include <array>
#include <cassert>
#include "ace_button/AceButton.h"
#include "timing.h"
using namespace ace_button;
#define HDS_ENABLE_GRINDER 0
#define HDS_FEATURE_WIFI 0
#define HDS_FEATURE_WEBSOCKET 0
constexpr int BUTTON_CIRCLE=1, BUTTON_SQUARE=2, BATTERY_CHARGING=3;
constexpr int U8G2_R0=0, U8G2_R2=2, FONT_M=0;
constexpr unsigned long BUTTON_POLL_INTERVAL_MS=2, CLICK_DELAY=180;
constexpr unsigned long DOUBLECLICK_DELAY=600, LONGPRESS_DELAY=900;
unsigned long clockMs=1000, t_shutdownFailBle=0, t_menuExitTime=0, menuChordPressedAt=0;
int GPIO_power_on_with=BUTTON_CIRCLE, i_buttonBootDelay=0;
int i_cal_weight=0, i_button_cal_status=0, menuMoves=0, menuSelects=0, sampledPresses=0;
bool b_menu=false, b_calibration=false, b_ota=false, b_powerOff=false;
bool b_showChargingUI=false, b_is_charging=true, b_ble_enabled=true, b_chargingOLED=false;
bool b_softSleep=false, b_u8g2Sleep=true, b_shutdownFailBle=false;
bool b_screenFlipped=false;
bool b_websocketLowPowerEnabled=false, b_buttonChordSuppressUntilRelease=false;
bool b_menuCirclePressPending=false, b_menuSquarePressPending=false;
bool b_menuCircleLongHandled=false, b_menuSquareLongHandled=false;
struct { bool active=false; } circle_press_data, square_press_data;
struct {
  bool explicitDisplayOff=false;
  int requestedDisplayContrast=255;
} energyRuntime;
struct {
  bool lightSleepButtonTracePending=false;
  unsigned long lastButtonPoll=0, lastLightSleepButtonPollAt=0, lastLightSleepButtonEventAt=0;
} energyIdle;
struct {
  bool powerSave=true;
  void setPowerSave(int value) { powerSave=value; }
  void setContrast(int) {}
  void setFontDirection(int) {}
  void setDisplayRotation(int) {}
} u8g2;
struct {
  template<class T> void print(T) {}
  template<class T> void println(T) {}
} Serial;
struct Menu {};
const Menu mainItem{};
const Menu *mainMenu[]={&mainItem};
const Menu **currentMenu=mainMenu, *currentSelection=&mainItem;
int currentMenuSize=1, currentIndex=0;
template<typename T, unsigned N> int getMenuSize(T (&)[N]) { return N; }
std::array<int, 4> pins={HIGH,HIGH,HIGH,HIGH};
unsigned long millis() { return clockMs; }
void delay(unsigned long duration) { clockMs+=duration; }
void serviceFuelGauge() {}
void refreshOLED(char*, int) {}
int digitalRead(uint8_t pin) { return pins[pin]; }
void pinMode(int, int) {}
bool usbPowerKeepsAwake() { return true; }
bool serviceEnergyButtonGesture(unsigned long) { return true; }
void power_off(int) {}
void recordEnergyActivity() {}
void invalidateMenuFrame() {}
void navigateMenu(int direction) { menuMoves+=direction; }
void selectMenu() { ++menuSelects; }
void backMenu() {}
bool bleHasLiveClient() { return false; }
void sendBlePowerOff(int) {}
void ble_init() {}
bool wakeScaleFromSoftSleep(const char*) { b_softSleep=false; return true; }
void startPressSampling(int pin) {
  ++sampledPresses;
  if (pin==BUTTON_CIRCLE) circle_press_data.active=true;
  else square_press_data.active=true;
}
void onButtonReleased(int) {}
int remoteTares=0, timerToggles=0;
void requestRemoteTare() { ++remoteTares; }
void toggleTimer() { ++timerToggles; }
ButtonConfig config1;
AceButton buttonCircle(&config1), buttonSquare(&config1);
void applyEnergyDisplayCommand(bool enabled) {
  b_u8g2Sleep=!enabled;
  u8g2.setPowerSave(!enabled);
}
void applyEnergyLowPowerCommand() {}
'''
    for signature in ("inline bool chargingUiActive()", "inline void requestEnergyDisplay(bool enabled)"):
        source += signature + " {" + block_after(parameter, signature) + "}\n"
    source += "\n".join(signature + ";" for signature in signatures)
    source += "\n".join(signature + " {" + block_after(hds, signature + " {") + "}"
                        for signature in signatures)
    source += "\nvoid usbTare() {" + block_after(usb, 'if (inputString.startsWith("tare"))') + "}\n"
    source += "\nvoid usbSet() {" + block_after(usb, 'if (inputString.startsWith("set"))') + "}\n"
    source += parameter[parameter.index("const uint32_t WSP_DISPLAY_ON"):parameter.index("portMUX_TYPE wsPendingMux")]
    source += "\nuint32_t pendingDisplay=0;\nvoid remoteQueuePending(uint32_t bits) { pendingDisplay|=bits; }\n"
    source += "\nvoid restoreDisplayAfterBleDisconnect() {" + block_after(ble, "void restoreDisplayAfterBleDisconnect() {") + "}\n"
    source += "\nvoid dispatchDisplay() { uint32_t mask=pendingDisplay; pendingDisplay=0;\n"
    source += "if (mask & WSP_DISPLAY_RESTORE) {" + block_after(ws, "if (mask & WSP_DISPLAY_RESTORE)") + "}\n"
    start = ws.index("#if HDS_ENABLE_ENERGY_MENU\n  if (mask & WSP_DISPLAY_ON)")
    source += ws[start:ws.index("  if (mask & WSP_SLEEP_ON)", start)] + "}\n"
    source += r'''
void poll(unsigned long duration) {
  for (unsigned long i=0; i<duration; ++i) {
    ++clockMs;
    serviceScaleButtonInputs(false);
  }
}
void boot(bool display, int quickBoot) {
  clockMs+=2000;
  pins.fill(HIGH);
  b_menu=b_calibration=b_ota=b_powerOff=b_buttonChordSuppressUntilRelease=false;
  b_menuCirclePressPending=b_menuSquarePressPending=false;
  b_menuCircleLongHandled=b_menuSquareLongHandled=false;
  b_softSleep=false;
  b_showChargingUI=false;
  GPIO_power_on_with=BUTTON_CIRCLE;
  b_chargingOLED=display;
  i_buttonBootDelay=quickBoot;
  remoteTares=timerToggles=menuMoves=menuSelects=sampledPresses=0;
  pendingDisplay=0;
  menuChordPressedAt=0;
  config1.setDebounceDelay(20);
  button_init();
  poll(50);
  enterUsbStandby();
  poll(50);
  assert(chargingUiActive() && !b_powerOff);
  assert(u8g2.powerSave!=display && b_u8g2Sleep!=display);
}
void tap(int pin, unsigned long held=60, unsigned long gap=80) {
  pins[pin]=LOW;
  poll(held);
  pins[pin]=HIGH;
  poll(gap);
}
void testWakeClicks(bool display, int quickBoot, int pin) {
  boot(display, quickBoot);
  clockMs=65500;
  tap(pin, quickBoot ? 1000 : 60);
  assert(GPIO_power_on_with==pin && !b_powerOff && sampledPresses==0);
  assert(!u8g2.powerSave && !b_u8g2Sleep);
  assert(buttonCircle.getLastButtonState()==HIGH && buttonSquare.getLastButtonState()==HIGH);
  assert(config1.getDebounceDelay()==20);
  tap(pin);
  assert(!b_powerOff && sampledPresses==1);
  tap(pin);
  assert(b_powerOff);
}
void testBlankStandby(bool display) {
  boot(display, 500);
  tap(BUTTON_CIRCLE);
  assert(chargingUiActive() && sampledPresses==0 && !b_powerOff);
  assert(u8g2.powerSave!=display && b_u8g2Sleep!=display);
  restoreDisplayAfterBleDisconnect();
  assert(u8g2.powerSave!=display && b_u8g2Sleep!=display);
  dispatchDisplay();
  assert(u8g2.powerSave!=display && b_u8g2Sleep!=display);
  GPIO_power_on_with=BUTTON_CIRCLE;
  b_showChargingUI=false;
  restoreDisplayAfterBleDisconnect();
  enterUsbStandby();
  dispatchDisplay();
  assert(chargingUiActive() && u8g2.powerSave!=display && b_u8g2Sleep!=display);
}
void testUsbCommands(bool display, int quickBoot) {
  for (const auto command : {usbTare, usbSet}) {
    boot(display, quickBoot);
    command();
    assert(remoteTares==0 && timerToggles==0 && sampledPresses==0);
    assert(chargingUiActive()==(quickBoot!=0));
  }
  boot(display, 0);
  tap(BUTTON_CIRCLE);
  usbTare();
  usbSet();
  assert(remoteTares==1 && timerToggles==1);
}
void testMenuChord(bool display, int quickBoot) {
  boot(display, quickBoot);
  pins[BUTTON_CIRCLE]=pins[BUTTON_SQUARE]=LOW;
  poll(510);
  assert(b_menu && !chargingUiActive() && !u8g2.powerSave && !b_powerOff);
  assert(currentMenu==mainMenu && currentSelection==mainMenu[0]);
  pins.fill(HIGH);
  poll(50);
  assert(menuMoves==0 && menuSelects==0 && sampledPresses==0);
  tap(BUTTON_CIRCLE);
  assert(menuMoves==1 && menuSelects==0 && !b_powerOff);
  tap(BUTTON_SQUARE);
  assert(menuMoves==1 && menuSelects==1 && !b_powerOff);
}
int main() {
  for (bool display : {false,true}) {
    testBlankStandby(display);
    for (int quickBoot : {0,500}) {
      for (int pin : {BUTTON_CIRCLE,BUTTON_SQUARE}) testWakeClicks(display,quickBoot,pin);
      testUsbCommands(display,quickBoot);
      testMenuChord(display,quickBoot);
    }
  }
}
'''
    with tempfile.TemporaryDirectory(prefix="usb-standby-") as directory:
        path = Path(directory)
        (path / "Arduino.h").write_text(
            "#pragma once\n#include <cstdint>\n#include <cstddef>\n"
            "#define HIGH 1\n#define LOW 0\n#define INPUT_PULLUP 2\n#define PROGMEM\n"
            "#define pgm_read_ptr(p) (*(p))\nclass __FlashStringHelper;\n"
            "unsigned long millis();\nint digitalRead(uint8_t);\n", encoding="utf-8")
        cpp, binary = path / "standby.cpp", path / "standby.exe"
        cpp.write_text(source, encoding="utf-8")
        for energy in (0, 1):
            subprocess.run([compiler, "-std=c++17", f"-DHDS_ENABLE_ENERGY_MENU={energy}",
                            "-I"+str(path), "-I"+str(ROOT / "include"), "-I"+str(library / "src"),
                            str(cpp), str(library / "src/ace_button/AceButton.cpp"),
                            str(library / "src/ace_button/ButtonConfig.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("USB standby, consumed wake/clicks, menu chord, OLED/BLE restore, and USB routing passed (AceButton 1.10.1)")


if __name__ == "__main__":
    main()
