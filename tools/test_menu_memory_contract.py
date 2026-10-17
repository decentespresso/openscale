#!/usr/bin/env python3
import re
import subprocess
import tempfile
from pathlib import Path

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]
MENU = ROOT / "include" / "menu.h"
SKETCH = ROOT / "src" / "hds.ino"
SUBMENU_LINKS = {
    "menuConnections": "menuConnectionsBack",
    "menuDisplay": "menuDisplayBack",
    "menuGrinder": "menuGrinderBack",
    "menuInfo": "menuInfoBack",
    "menuPower": "menuPowerBack",
    "menuScale": "menuScaleBack",
}

OBSOLETE_SUBMENUS = (
    "buzzerMenu",
    "heartbeatMenu",
    "flipScreenMenu",
    "timeOnTopMenu",
    "btnFuncWhileConnectedMenu",
    "autoSleepMenu",
    "quickBootMenu",
    "driftCompMenu",
    "tapActionsMenu",
    "energyMenu",
)


def main():
    menu = MENU.read_text(encoding="utf-8")
    sketch = SKETCH.read_text(encoding="utf-8")
    declarations = re.findall(
        r"^const Menu (menu\w+) = \{(.*?)\};",
        menu,
        flags=re.MULTILINE | re.DOTALL,
    )
    links = {}
    for name, initializer in declarations:
        match = re.search(r",\s*(&menu\w+|NULL)\s*,\s*(?:&menu\w+|NULL)\s*$", initializer)
        assert match is not None
        if match.group(1) != "NULL":
            links[name] = match.group(1)[1:]

    assert links == SUBMENU_LINKS
    assert "\nMenu menu" not in menu
    assert re.search(r"^Menu \*const", menu, flags=re.MULTILINE) is None
    assert "currentSelection->subMenu" in menu
    assert "linkSubmenus" not in menu
    assert "linkSubmenus" not in sketch
    assert all(name not in menu for name in OBSOLETE_SUBMENUS)
    assert "const Menu *const scaleMenu[]" in menu
    assert "const Menu *const connectionsMenu[]" in menu
    assert "const Menu *const displayMenu[]" in menu
    assert "const Menu *const powerMenu[]" in menu
    assert "const Menu *const infoMenu[]" in menu
    assert re.search(
        r"#if HDS_ENABLE_GRINDER\s+&menuGrinder,\s+#endif", menu
    )
    assert re.search(
        r"#if HDS_ENABLE_ENERGY_MENU\s+&menuEnergyOledRedraw", menu
    )
    assert "void backMenu()" in menu
    assert "navigateMenu(-1);" in sketch
    for view in ("showWifiStatus", "showStatus", "showAbout", "showLogo"):
        body = menu[menu.rindex(f"void {view}()") :]
        body = body[: body.index("\n}")]
        assert "waitForMenuButtonRelease();" not in body
        assert "delay(" not in body
        assert "while (b_show" not in body
    pressed = sketch[sketch.index("case AceButton::kEventPressed:") :]
    pressed = pressed[: pressed.index("case AceButton::kEventDoubleClicked:")]
    menu_pressed = pressed[pressed.index("if (b_menu) {") :]
    assert "recordEnergyActivity();" in menu_pressed

    callback = block_after(sketch, "void aceButtonHandleEvent(")
    assert callback.index("handleMenuInfoPageButton(pin, eventType)") < callback.index("switch (eventType)")
    loop = block_after(sketch, "void loop() {")
    assert "if (!b_menu && menuInfoPageActive()) clearMenuInfoPage();" in loop
    resume = loop[loop.index("if (otaTransportsStopped)"):loop.index("processBleStatusResponse();")]
    assert "invalidateMenuFrame();" in resume
    check_info_pages(menu)

    print("menu memory contract tests passed")


def check_info_pages(menu):
    source = r'''
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include "timing.h"
using String = std::string;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2;
struct AceButton {
  enum { kEventPressed, kEventReleased, kEventLongPressed, kEventDoubleClicked };
};
bool b_menu = true, b_showLogo = false, b_showWifiData = false;
bool b_showStatusData = false, b_showNumber = false, b_negativeWeight = false;
bool b_menuCirclePressPending = false, b_menuSquarePressPending = false;
bool b_menuCircleLongHandled = false, b_menuSquareLongHandled = false;
std::atomic<bool> b_showAbout{false};
bool b_wifiEnabled = true, b_wifiOnBoot = true, b_ble_enabled = true;
bool b_btnFuncWhileConnected = true, b_requireHeartBeat = true;
bool b_autoSleep = true, b_quickBoot = false, b_timeOnTop = false, b_screenFlipped = false;
float f_maxDriftCompensation = 0.05f;
String actionMessage = "Default", actionMessage2 = "Default";
unsigned long clockMs = 5000, t_actionMessage = 0;
int t_actionMessageDelay = 1000;
constexpr unsigned long MENU_SAFETY_REFRESH_INTERVAL_MS = 100;
bool menuFrameDirty = true, menuFrameShowsActionMessage = false;
unsigned long lastMenuFrameRender = 0;
unsigned long millis() { return clockMs; }
void delay(int) { assert(false); }
void waitForMenuButtonRelease() { assert(false); }
void invalidateEnergyOledFrame() {}
constexpr int FONT_S = 0, FONT_M = 0, FONT_TIMER = 0, FONT_GRAM = 0, FONT_WEIGHT = 0;
constexpr int u8g2_font_6x12_tr = 0, u8g2_font_5x7_tr = 0, u8g2_font_logisoso22_tf = 0;
constexpr int LCDWidth = 128, LCDHeight = 64, U8G2_R0 = 0, U8G2_R2 = 2;
const unsigned char image_battery_4[] = {0}, image_ble_enabled[] = {0};
#define FIRMWARE_VER "FW: 3.1.15"
#define LINE3 "2026-10-17"
#define GIT_REV "test"
#define AC(value) 0
#define AR(value) 0
#define AM() 32
#define AT() 0
char *trim(char *value) { return value; }
struct {
  int frames = 0;
  bool inFrame = false;
  void firstPage() { assert(!inFrame); inFrame = true; ++frames; }
  bool nextPage() { assert(inFrame); inFrame = false; return false; }
  void setFont(int) {}
  void setDisplayRotation(int) {}
  int getUTF8Width(const char *value) { return std::strlen(value) * 6; }
  int getMaxCharHeight() { return 12; }
  void drawStr(int, int, const char *) { assert(inFrame); }
  void drawBox(int, int, int, int) { assert(inFrame); }
  void drawXBM(int, int, int, int, const unsigned char *) { assert(inFrame); }
} u8g2;
struct {
  bool isConnected() { return true; }
  String SSID() { return "network"; }
  struct Address { String toString() { return "192.168.1.2"; } };
  Address localIP() { return {}; }
  int getMode() { return 1; }
  int status() { return 1; }
} WiFi;
constexpr int WIFI_AP = 2, WL_CONNECTED = 1, MDNS_NAME_OLED_LINE1_BYTES = 32;
bool wifiCredentialsSaved() { return true; }
const char *wifiDeviceName() { return "hds"; }
const char *mdnsNameSplitOledLine1(const char *name, char *output, size_t size) {
  std::snprintf(output, size, "%s", name);
  return "";
}
struct Menu {
  const char *name;
  void (*action)() = nullptr;
  const Menu *subMenu = nullptr;
  const Menu *parentMenu = nullptr;
};
const Menu item = {"Info"};
const Menu menuScale{"Scale"}, menuConnections{"Connections"}, menuDisplay{"Display"};
const Menu menuPower{"Power"}, menuInfo{"Info"};
const Menu *const mainMenu[] = {&item};
const Menu *const scaleMenu[] = {&menuScale}, *const connectionsMenu[] = {&menuConnections};
const Menu *const displayMenu[] = {&menuDisplay}, *const powerMenu[] = {&menuPower};
const Menu *const infoMenu[] = {&menuInfo};
const Menu *const *currentMenu = mainMenu;
const Menu *currentSelection = &item;
int currentIndex = 0, currentMenuSize = 1, currentPage = 0, totalPages = 0, linesPerPage = 4;
template<typename T> int getMenuSize(T &values) { return sizeof(values) / sizeof(values[0]); }
int connectionsMenuSize() { return 1; }
struct { void print(const char *) {} void println(int) {} } Serial;
void recordEnergyActivity() {}
void backMenu() { assert(false); }
void refreshMenuRows() {}
void invalidateMenuFrame() { @INVALIDATE@ }
bool menuFrameNeedsRender(unsigned long now) { @NEEDS_RENDER@ }
bool menuInfoPageActive() { @ACTIVE@ }
void clearMenuInfoPage() { @CLEAR@ }
void dismissMenuInfoPage() { @DISMISS@ }
bool handleMenuInfoPageButton(int pin, uint8_t eventType) { @BUTTON@ }
void showWifiStatus() { @WIFI@ }
void showStatus() { @STATUS@ }
void showAbout() { @ABOUT@ }
void showLogo() { @LOGO@ }
bool renderMenuInfoPage() { @RENDER@ }
void showMenu() { @MENU@ }
void navigateMenu(int direction) { @NAVIGATE@ }
void selectMenu() { @SELECT@ }
int main() {
  for (const auto open : {showStatus, showAbout, showLogo
#if HDS_FEATURE_WIFI
                          , showWifiStatus
#endif
                         }) {
    clearMenuInfoPage();
    open();
    assert(menuInfoPageActive());
    const Menu *selectionBefore = currentSelection;
    navigateMenu(1);
    assert(currentSelection == selectionBefore && currentIndex == 0);
    const int before = u8g2.frames;
    for (int tick = 0; tick < 100; ++tick) {
      clockMs += 500;
      showMenu();
      assert(menuInfoPageActive() && u8g2.frames == before);
    }
    invalidateMenuFrame();
    showMenu();
    assert(menuInfoPageActive() && u8g2.frames == before + 1);
    const bool wasLogo = b_showLogo, wasNumber = b_showNumber;
    for (const int event : {AceButton::kEventPressed, AceButton::kEventLongPressed,
                            AceButton::kEventDoubleClicked}) {
      assert(handleMenuInfoPageButton(BUTTON_SQUARE, event));
      assert(menuInfoPageActive());
    }
    assert(handleMenuInfoPageButton(BUTTON_CIRCLE, AceButton::kEventReleased));
    assert(menuInfoPageActive());
    b_menuCirclePressPending = b_menuSquarePressPending = true;
    b_menuCircleLongHandled = b_menuSquareLongHandled = true;
    assert(handleMenuInfoPageButton(BUTTON_SQUARE, AceButton::kEventReleased));
    assert(!menuInfoPageActive() && menuFrameDirty);
    assert(!b_menuCirclePressPending && !b_menuSquarePressPending);
    assert(!b_menuCircleLongHandled && !b_menuSquareLongHandled);
    assert(b_showNumber == (wasNumber != wasLogo));
    assert(!handleMenuInfoPageButton(BUTTON_SQUARE, AceButton::kEventReleased));
    showMenu();
    assert(u8g2.frames == before + 2);
  }
  showAbout();
  b_showAbout = false;
  clockMs += 1000;
  const int before = u8g2.frames;
  showMenu();
  assert(!menuInfoPageActive() && u8g2.frames == before + 1);
  showLogo();
  const bool wasNumber = b_showNumber;
  selectMenu();
  assert(!menuInfoPageActive() && b_showNumber != wasNumber);
  showStatus();
  b_menu = false;
  if (!b_menu && menuInfoPageActive()) clearMenuInfoPage();
  assert(!menuInfoPageActive());
}
'''
    replacements = {
        "INVALIDATE": "inline void invalidateMenuFrame()",
        "NEEDS_RENDER": "inline bool menuFrameNeedsRender(",
        "ACTIVE": "bool menuInfoPageActive()",
        "CLEAR": "void clearMenuInfoPage()",
        "DISMISS": "void dismissMenuInfoPage()",
        "BUTTON": "bool handleMenuInfoPageButton(",
        "WIFI": "void showWifiStatus() {",
        "STATUS": "void showStatus() {",
        "ABOUT": "void showAbout() {",
        "LOGO": "void showLogo() {",
        "RENDER": "bool renderMenuInfoPage()",
        "MENU": "void showMenu() {",
        "NAVIGATE": "void navigateMenu(int direction) {",
        "SELECT": "void selectMenu() {",
    }
    for marker, signature in replacements.items():
        source = source.replace(f"@{marker}@", block_after(menu, signature))
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "menu_info.cpp"
        binary = Path(directory) / "menu_info.exe"
        cpp.write_text(source, encoding="utf-8")
        for wifi in (0, 1):
            subprocess.run(["g++", "-std=c++17", "-I", str(ROOT / "include"),
                            f"-DHDS_FEATURE_WIFI={wifi}", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
