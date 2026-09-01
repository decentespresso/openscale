#ifndef MENU_H
#define MENU_H

#include "esp32-hal.h"
#include "parameter.h"
#include "timing.h"
#if HDS_FEATURE_WIFI
#include "mdns_name.h"
#include "wifi_setup.h"
#endif
#if HDS_ENABLE_GRINDER
#include "grinder_runtime.h"
#endif
#include <string.h>
const char *const weights[] = { "Exit", "50g", "100g", "200g", "500g", "1000g" };
const float weight_values[] = { 0.0, 50.0, 100.0, 200.0, 500.0, 1000.0 };
bool b_showAbout = false;
bool b_showLogo = false;
bool b_showNumber = false;
bool b_showWifiData = false;
bool b_showStatusData = false;
String actionMessage = "Default";
String actionMessage2 = "Default";
unsigned long t_actionMessage = 0;
int t_actionMessageDelay = 1000;
constexpr unsigned long MENU_SAFETY_REFRESH_INTERVAL_MS = 100;
bool menuFrameDirty = true;
bool menuFrameShowsActionMessage = false;
unsigned long lastMenuFrameRender = 0;

inline void invalidateMenuFrame() {
  menuFrameDirty = true;
}

inline bool menuFrameNeedsRender(unsigned long now) {
  const bool actionMessageVisible = now - t_actionMessage < t_actionMessageDelay;
  return menuFrameDirty
      || hdsIntervalElapsed(now, lastMenuFrameRender, MENU_SAFETY_REFRESH_INTERVAL_MS)
      || (menuFrameShowsActionMessage && !actionMessageVisible);
}

inline void menuActionMessageChanged() {
  t_actionMessage = millis();
  invalidateMenuFrame();
}

void waitForMenuButtonRelease() {
  while (digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW) {
    delay(20);
  }
}

template<typename T> int getMenuSize(T &menu) {
  return sizeof(menu) / sizeof(menu[0]);
}

struct Menu {
  const char *name;
  void (*action)();
  const Menu *subMenu;
  const Menu *parentMenu;
};

void exitMenu();
void backMenu();
#ifdef BUZZER
void toggleBuzzer();
#endif
#if HDS_FEATURE_WIFI
void toggleWifi();
void resetWifi();
void showWifiStatus();
#endif
void toggleHeartbeat();
void calibrate();
void drawButton();
#if HDS_FEATURE_PULL_OTA
void wifiUpdate();
void wifiUpdate(const PullOtaTargetVersion &target);
void customBuildMenu();
#endif
void showStatus();
void showAbout();
void showMenu();
void showLogo();
void calibrateVoltage();
void navigateMenu(int direction);
void selectMenu();
void enableDebug();
void toggleFlipScreen();
void toggleTimeOnTop();
void toggleBtnFuncWhileConnected();
void toggleAutoSleep();
void toggleQuickBoot();
void cycleDriftComp();
void toggleTapTare();
void toggleTapTimer();
void refreshMenuRows();
#if HDS_ENABLE_GRINDER
void toggleGrinder();
void grinderSelectPlugMenu();
void grinderTargetMenu();
void grinderSafetyMenu();
void grinderZeroRangeMenu();
#endif
#if HDS_FEATURE_WIFI
void wifi_init();
#endif

extern const Menu menuScaleBack;
extern const Menu menuConnectionsBack;
extern const Menu menuDisplayBack;
extern const Menu menuPowerBack;
extern const Menu menuInfoBack;
#if HDS_ENABLE_GRINDER
extern const Menu menuGrinderBack;
#endif

const Menu menuExit = { "Exit", exitMenu, NULL, NULL };
const Menu menuScale = { "Scale", NULL, &menuScaleBack, NULL };
const Menu menuConnections = { "Connections", NULL, &menuConnectionsBack, NULL };
const Menu menuDisplay = { "Display", NULL, &menuDisplayBack, NULL };
const Menu menuPower = { "Power", NULL, &menuPowerBack, NULL };
const Menu menuInfo = { "Info", NULL, &menuInfoBack, NULL };
const Menu menuStatus = { "Status", showStatus, NULL, &menuInfo };
const Menu menuAbout = { "About", showAbout, NULL, &menuInfo };
const Menu menuLogo = { "Show Logo", showLogo, NULL, &menuDisplay };
#if HDS_ENABLE_GRINDER
const Menu menuGrinder = { "Grind by weight", NULL, &menuGrinderBack, NULL };
#endif

char menuDriftLabel[18] = "Drift: 0.05g";
char menuTapTareLabel[] = "2x Tap Tare o";
char menuTapTimerLabel[] = "3x Tap Timer o";
char menuHeartbeatLabel[] = "Heartbeat o";
char menuBleButtonsLabel[] = "BLE Buttons o";
char menuFlipScreenLabel[] = "Flip Screen o";
char menuTimeOnTopLabel[] = "Top: Weight";
char menuAutoSleepLabel[] = "Auto Sleep o";
char menuQuickBootLabel[] = "Quick Boot o";
#if HDS_FEATURE_WIFI
char menuWifiLabel[] = "WiFi o";
#endif
#ifdef BUZZER
char menuBuzzerLabel[] = "Buzzer o";
#endif

const Menu menuScaleBack = { "Back", NULL, NULL, &menuScale };
const Menu menuCalibrate = { "Calibrate", calibrate, NULL, &menuScale };
const Menu menuDriftComp = { menuDriftLabel, cycleDriftComp, NULL, &menuScale };
const Menu menuTapTare = { menuTapTareLabel, toggleTapTare, NULL, &menuScale };
const Menu menuTapTimer = { menuTapTimerLabel, toggleTapTimer, NULL, &menuScale };
const Menu *const scaleMenu[] = { &menuScaleBack, &menuCalibrate, &menuDriftComp,
                                 &menuTapTare, &menuTapTimer };

const Menu menuConnectionsBack = { "Back", NULL, NULL, &menuConnections };
#if HDS_FEATURE_WIFI
const Menu menuWifiToggle = { menuWifiLabel, toggleWifi, NULL, &menuConnections };
const Menu menuWiFiStatusOption = { "WiFi Status", showWifiStatus, NULL, &menuConnections };
#if HDS_FEATURE_PULL_OTA
const Menu menuWiFiPullUpdateOption = { "WiFi Update", wifiUpdate, NULL, &menuConnections };
const Menu menuCustomBuildOption = { "Custom Build", customBuildMenu, NULL, &menuConnections };
#endif
const Menu menuWiFiResetOption = { "Reset WiFi", resetWifi, NULL, &menuConnections };
#endif
const Menu menuHeartbeat = { menuHeartbeatLabel, toggleHeartbeat, NULL, &menuConnections };
const Menu menuBleButtons = { menuBleButtonsLabel, toggleBtnFuncWhileConnected, NULL, &menuConnections };
const Menu *const connectionsMenu[] = {
  &menuConnectionsBack,
#if HDS_FEATURE_WIFI
  &menuWifiToggle,
#endif
  &menuHeartbeat,
  &menuBleButtons,
#if HDS_FEATURE_WIFI
  &menuWiFiStatusOption,
#if HDS_FEATURE_PULL_OTA
  &menuWiFiPullUpdateOption,
  &menuCustomBuildOption,
#endif
  &menuWiFiResetOption,
#endif
};

const Menu menuDisplayBack = { "Back", NULL, NULL, &menuDisplay };
const Menu menuFlipScreen = { menuFlipScreenLabel, toggleFlipScreen, NULL, &menuDisplay };
const Menu menuTimeOnTop = { menuTimeOnTopLabel, toggleTimeOnTop, NULL, &menuDisplay };
#ifdef BUZZER
const Menu menuBuzzer = { menuBuzzerLabel, toggleBuzzer, NULL, &menuDisplay };
#endif
const Menu *const displayMenu[] = { &menuDisplayBack, &menuFlipScreen,
                                   &menuTimeOnTop,
#ifdef BUZZER
                                   &menuBuzzer,
#endif
                                   &menuLogo };

const Menu menuPowerBack = { "Back", NULL, NULL, &menuPower };
const Menu menuAutoSleep = { menuAutoSleepLabel, toggleAutoSleep, NULL, &menuPower };
const Menu menuQuickBoot = { menuQuickBootLabel, toggleQuickBoot, NULL, &menuPower };
#if HDS_ENABLE_ENERGY_MENU
#include "energy_menu.h"
#endif
const Menu *const powerMenu[] = { &menuPowerBack, &menuAutoSleep, &menuQuickBoot,
#if HDS_ENABLE_ENERGY_MENU
                                  &menuEnergySerialQuiet, &menuEnergyOledRedraw,
                                  &menuEnergyOledIdle, &menuEnergyLightSleep,
                                  &menuEnergyUsbSleepTest,
#endif
};

const Menu menuInfoBack = { "Back", NULL, NULL, &menuInfo };
const Menu *const infoMenu[] = { &menuInfoBack, &menuStatus, &menuAbout };

#if HDS_ENABLE_GRINDER
const Menu menuGrinderBack = { "Back", NULL, NULL, &menuGrinder };
char menuGrinderEnabledLabel[] = "Enabled o";
char menuGrinderTargetLabel[22] = "Target: 15.0g";
char menuGrinderSafetyLabel[22] = "Safety: 2.0g";
char menuGrinderZeroLabel[22] = "Zero Range: 1.0g";
const Menu menuGrinderEnabled = { menuGrinderEnabledLabel, toggleGrinder, NULL, &menuGrinder };
const Menu menuGrinderSelect = { "Select Plug", grinderSelectPlugMenu, NULL, &menuGrinder };
const Menu menuGrinderTarget = { menuGrinderTargetLabel, grinderTargetMenu, NULL, &menuGrinder };
const Menu menuGrinderSafety = { menuGrinderSafetyLabel, grinderSafetyMenu, NULL, &menuGrinder };
const Menu menuGrinderZero = { menuGrinderZeroLabel, grinderZeroRangeMenu, NULL, &menuGrinder };
const Menu *const grinderMenu[] = { &menuGrinderBack, &menuGrinderEnabled,
                                   &menuGrinderSelect, &menuGrinderTarget,
                                   &menuGrinderSafety, &menuGrinderZero };
#endif

const Menu *const mainMenu[] = {
  &menuExit,
  &menuScale,
  &menuConnections,
  &menuDisplay,
  &menuPower,
#if HDS_ENABLE_GRINDER
  &menuGrinder,
#endif
  &menuInfo,
};
const Menu *const *currentMenu = mainMenu;
const Menu *currentSelection = mainMenu[0];
int currentMenuSize = getMenuSize(mainMenu);
int currentIndex = 0;
const int linesPerPage =
  4;
int currentPage = 0;
int totalPages = currentMenuSize / linesPerPage + 1;

void exitMenu() {
  invalidateMenuFrame();
  u8g2.setFont(FONT_M);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"Exit Menu"), AM(), (char *)"Exit Menu");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
#if HDS_ENABLE_GRINDER
  grinderResumeAfterMenu();
#endif
  leaveMenu();
#if HDS_ENABLE_GRINDER
  b_grinderMenuDirectEntry = false;
#endif
  currentMenu = mainMenu;
  currentMenuSize = getMenuSize(mainMenu);
  currentIndex = 0;
  currentSelection = currentMenu[currentIndex];
  t_menuExitTime = millis();
}

inline void updateToggleLabel(char *label, bool enabled) {
  label[strlen(label) - 1] = enabled ? 'x' : 'o';
}

inline void showStoredAction(const char *label, bool enabled, bool stored) {
  actionMessage = stored ? String(label) : "Save Failed";
  actionMessage2 = stored ? (enabled ? "ON" : "OFF") : String(label);
  menuActionMessageChanged();
  t_actionMessageDelay = 1000;
}

template<typename State>
bool toggleStoredBool(State &state, const char *key, const char *label,
                      char *row) {
  const bool enabled = !static_cast<bool>(state);
  const bool stored = storagePutBool(key, enabled);
  if (stored) state = enabled;
  updateToggleLabel(row, static_cast<bool>(state));
  showStoredAction(label, enabled, stored);
  return stored;
}

#ifdef BUZZER
void toggleBuzzer() {
  const bool enabled = !static_cast<bool>(b_beep);
  const bool stored = storagePutInt(KEY_BEEP, enabled);
  if (stored) {
    b_beep = enabled;
    if (enabled) buzzer.beep(1, BUZZER_DURATION);
  }
  updateToggleLabel(menuBuzzerLabel, b_beep);
  showStoredAction("Buzzer", enabled, stored);
}
#endif

#if HDS_FEATURE_WIFI
void toggleWifi() {
  const bool enabled = !b_wifiOnBoot;
#if HDS_ENABLE_GRINDER
  if (!enabled && grinderSettings.enabled) {
    grinderSettings.previousWifiOnBoot = false;
    grinderSettings.previousWifiOnBootSaved = true;
    grinderSaveSettings();
    actionMessage = "Grind by weight";
    actionMessage2 = "Needs WiFi";
    menuActionMessageChanged();
    t_actionMessageDelay = 1500;
    Serial.println("WiFi Off deferred until Grind by weight is disabled.");
    return;
  }
#endif
  const bool stored = storagePutBool(KEY_WIFI_BOOT, enabled);
  if (stored) {
    b_wifiOnBoot = enabled;
#if HDS_ENABLE_GRINDER
    if (grinderSettings.enabled) {
      grinderSettings.previousWifiOnBoot = enabled;
      grinderSettings.previousWifiOnBootSaved = true;
      grinderSaveSettings();
    }
#endif
    markMenuRestartRequired();
  }
  updateToggleLabel(menuWifiLabel, b_wifiOnBoot);
  showStoredAction("WiFi", enabled, stored);
}

void showWifiStatus() {
  b_showWifiData = true;

  String ssid = WiFi.isConnected() ? WiFi.SSID() : "N/A";
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
  const char *status = WiFi.isConnected() ? "Enabled" : "Disabled";

  char nameLine1[MDNS_NAME_OLED_LINE1_BYTES];
  const char *nameLine2 =
    mdnsNameSplitOledLine1(wifiDeviceName(), nameLine1, sizeof(nameLine1));

#if HDS_ENABLE_ENERGY_MENU
  invalidateEnergyOledFrame();
#endif
  u8g2.firstPage();
  do {

    u8g2.setFont(u8g2_font_6x12_tr);

    u8g2.drawStr(0, 10, "WiFi Status:");
    u8g2.drawStr(72, 10, status);

    u8g2.drawStr(0, 22, "SSID:");
    u8g2.drawStr(40, 22, ssid.c_str());

    u8g2.drawStr(0, 34, "IP:");
    u8g2.drawStr(40, 34, ip.c_str());

    u8g2.drawStr(0, 46, "Name:");
    u8g2.drawStr(40, 46, nameLine1);
    if (nameLine2[0] != 0) {
      u8g2.drawStr(0, 58, nameLine2);
    }

  } while (u8g2.nextPage());
  delay(1000);
  while (b_showWifiData) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      b_showWifiData = false;
    }
  }
  waitForMenuButtonRelease();
}
#endif

void showStatus() {
  b_showStatusData = true;

  char wifiLine[32];
  char bleLine[32];
  char sleepLine[32];
  char driftLine[32];
#if HDS_ENABLE_GRINDER
  char grinderLine[32];
#endif
#if HDS_FEATURE_WIFI
  const char *wifiRunState = "Idle";
  if (b_wifiEnabled) {
    if (WiFi.getMode() == WIFI_AP) {
      wifiRunState = "AP";
    } else if (WiFi.status() == WL_CONNECTED) {
      wifiRunState = "Conn";
    } else {
      wifiRunState = "Wait";
    }
  }

  snprintf(wifiLine, sizeof(wifiLine), "WiFi:%s %s %s",
           b_wifiOnBoot ? "On" : "Off",
           wifiCredentialsSaved() ? "Saved" : "NoCred",
           wifiRunState);
#else
  snprintf(wifiLine, sizeof(wifiLine), "WiFi:Unavailable");
#endif
  snprintf(bleLine, sizeof(bleLine), "BLE:%s Btn:%s HB:%s",
           b_ble_enabled ? "On" : "Off",
           b_btnFuncWhileConnected ? "On" : "Off",
           b_requireHeartBeat ? "On" : "Off");
  snprintf(sleepLine, sizeof(sleepLine), "Sleep:%s Quick:%s",
           b_autoSleep ? "On" : "Off",
           b_quickBoot ? "On" : "Off");
  snprintf(driftLine, sizeof(driftLine), "T:%s Drift:%.3fg",
           b_timeOnTop ? "Top" : "Bot",
           f_maxDriftCompensation);
#if HDS_ENABLE_GRINDER
  snprintf(grinderLine, sizeof(grinderLine), "Gr:%s %.1fg",
           grinderRuntime.status[0] ? grinderRuntime.status : grinderStateText(grinderRuntime.state),
           grinderSettings.targetGrams);
#endif

  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "Status");
    u8g2.drawStr(0, 22, wifiLine);
    u8g2.drawStr(0, 34, bleLine);
    u8g2.drawStr(0, 46, sleepLine);
#if HDS_ENABLE_GRINDER
    u8g2.drawStr(0, 58, grinderSettings.enabled ? grinderLine : driftLine);
#else
    u8g2.drawStr(0, 58, driftLine);
#endif
  } while (u8g2.nextPage());
  delay(1000);
  while (b_showStatusData) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      b_showStatusData = false;
    }
  }
  waitForMenuButtonRelease();
}

#if HDS_FEATURE_WIFI
void resetWifi() {
  saveCredentials("", "");
  actionMessage = "WiFi Reset";
  actionMessage2 = "Restart on exit";
  menuActionMessageChanged();
  t_actionMessageDelay = 1000;
  markMenuRestartRequired();
}
#endif

void toggleHeartbeat() {
  toggleStoredBool(b_requireHeartBeat, KEY_HEARTBEAT, "Heartbeat",
                   menuHeartbeatLabel);
}

void toggleTapTare() {
  toggleStoredBool(b_tapTareEnabled, KEY_TAP_TARE, "2x Tap Tare",
                   menuTapTareLabel);
}

void toggleTapTimer() {
  toggleStoredBool(b_tapTimerEnabled, KEY_TAP_TIMER, "3x Tap Timer",
                   menuTapTimerLabel);
}

void toggleFlipScreen() {
  if (toggleStoredBool(b_screenFlipped, KEY_SCREEN_FLIP, "Flip Screen",
                       menuFlipScreenLabel)) {
    u8g2.setDisplayRotation(b_screenFlipped ? U8G2_R0 : U8G2_R2);
  }
}

void toggleTimeOnTop() {
  const bool enabled = !b_timeOnTop;
  const bool stored = storagePutBool(KEY_TIME_ON_TOP, enabled);
  if (stored) b_timeOnTop = enabled;
  snprintf(menuTimeOnTopLabel, sizeof(menuTimeOnTopLabel), "Top: %s",
           b_timeOnTop ? "Time" : "Weight");
  showStoredAction("Top", enabled, stored);
}

void toggleBtnFuncWhileConnected() {
  toggleStoredBool(b_btnFuncWhileConnected, KEY_BTN_CONN, "BLE Buttons",
                   menuBleButtonsLabel);
}

void toggleAutoSleep() {
  toggleStoredBool(b_autoSleep, KEY_AUTO_SLEEP, "Auto Sleep",
                   menuAutoSleepLabel);
}

void toggleQuickBoot() {
  toggleStoredBool(b_quickBoot, KEY_QUICK_BOOT, "Quick Boot",
                   menuQuickBootLabel);
}

void cycleDriftComp() {
  constexpr float values[] = { 0.0f, 0.05f, 0.075f, 0.10f, 0.20f };
  uint8_t current = 0;
  for (uint8_t index = 0; index < getMenuSize(values); ++index) {
    if (fabsf(f_maxDriftCompensation - values[index]) < 0.0001f) current = index;
  }
  const float next = values[(current + 1) % getMenuSize(values)];
  const bool stored = storagePutFloat(KEY_DRIFT_MAX, next);
  if (stored) f_maxDriftCompensation = next;
  refreshMenuRows();
  actionMessage = stored ? "Drift" : "Save Failed";
  actionMessage2 = stored ? String(menuDriftLabel + 7) : "Drift";
  menuActionMessageChanged();
  t_actionMessageDelay = 1000;
}

#if HDS_ENABLE_GRINDER
void grinderSetActionMessage(const char *line1, const char *line2 = nullptr) {
  actionMessage = line1;
  actionMessage2 = line2 == nullptr ? "Default" : line2;
  menuActionMessageChanged();
  t_actionMessageDelay = 1500;
}

void toggleGrinder() {
  if (!grinderSettings.enabled) {
    if (!grinderSettings.previousWifiOnBootSaved) {
      grinderSettings.previousWifiOnBoot = b_wifiOnBoot;
      grinderSettings.previousWifiOnBootSaved = true;
    }
    grinderSetEnabled(true);
    b_wifiOnBoot = true;
    storagePutBool(KEY_WIFI_BOOT, true);
    if (!b_wifiEnabled) wifi_init();
    refreshMenuRows();
    grinderSetActionMessage("Grind by weight", "Enabled");
    return;
  }
  const bool restoreWifiOnBoot = grinderSettings.previousWifiOnBoot;
  const bool restoreWifiOnBootSaved = grinderSettings.previousWifiOnBootSaved;
  grinderSetEnabled(false);
  if (restoreWifiOnBootSaved) {
    b_wifiOnBoot = restoreWifiOnBoot;
    storagePutBool(KEY_WIFI_BOOT, restoreWifiOnBoot);
    grinderSettings.previousWifiOnBoot = false;
    grinderSettings.previousWifiOnBootSaved = false;
    grinderSaveSettings();
  }
  refreshMenuRows();
  grinderSetActionMessage("Grind by weight", "Disabled");
}

bool grinderEnsureWifiReadyForDiscovery() {
  if (!b_wifiEnabled) {
    b_wifiOnBoot = true;
    wifi_init();
  }
  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 12000) {
    refreshOLED((char *)"WiFi", (char *)"Connecting");
    if (b_wifiEnabled) {
      wifiSupervise();
    }
    delay(250);
  }
  if (b_wifiEnabled) {
    wifiSupervise();
  }
  return WiFi.status() == WL_CONNECTED;
}

uint8_t grinderFindPlugsForSelection() {
  if (!grinderEnsureWifiReadyForDiscovery()) {
    grinderSetActionMessage("WiFi Wait", "No Plugs");
    return 0;
  }
  grinderReleaseClientForDiscovery();
  refreshOLED((char *)"Finding", (char *)"Plugs");
  uint8_t count = grinderDiscoverPlugs();
  char message[24];
  if (count == 0) {
    grinderSetActionMessage(grinderRuntime.status[0] ? grinderRuntime.status : "No Plugs");
  } else {
    snprintf(message, sizeof(message), "Found %u", count);
    grinderSetActionMessage(message);
  }
  return count;
}

void grinderDrawPlugList(uint8_t selected) {
  const uint8_t total = grinderRuntime.discoveredCount + 1;
  const uint8_t rows = 6;
  uint8_t first = (selected / rows) * rows;
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_5x8_tr);
    for (uint8_t row = 0; row < rows; row++) {
      const uint8_t choice = first + row;
      if (choice >= total) {
        break;
      }
      const uint8_t y = 9 + row * 10;
      if (choice == selected) {
        u8g2.drawStr(0, y, ">");
      }
      if (choice == 0) {
        u8g2.drawStr(8, y, "Back");
      } else {
        u8g2.drawStr(8, y, grinderRuntime.discovered[choice - 1].mac);
      }
    }
  } while (u8g2.nextPage());
}

void grinderSelectPlugMenu() {
  if (grinderFindPlugsForSelection() == 0) {
    return;
  }
  waitForMenuButtonRelease();
  uint8_t selected = 0;
  bool selecting = true;
  while (selecting) {
    power_off(-1);
    grinderDrawPlugList(selected);
    if (digitalRead(BUTTON_CIRCLE) == LOW) {
      selected = (selected + 1) % (grinderRuntime.discoveredCount + 1);
      waitForMenuButtonRelease();
    }
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      if (selected == 0) {
        selecting = false;
      } else {
        grinderSaveSelectedDiscovery(grinderRuntime.discovered[selected - 1]);
        grinderRuntimeReset();
        Serial.printf("[grinder] selected mac=%s host=%s ip=%s enabled=%d\n",
                      grinderSettings.selectedMac,
                      grinderSettings.hostname,
                      grinderSettings.lastIp.toString().c_str(),
                      grinderSettings.enabled ? 1 : 0);
        grinderSetActionMessage("Plug Selected", grinderSettings.enabled ? "Saved" : "Enable in menu");
        selecting = false;
      }
      waitForMenuButtonRelease();
    }
    delay(40);
  }
}

static inline float grinderClampDraft(float value, float minValue, float maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static inline void grinderDrawNumberEditor(const char *title, float value, uint8_t selected) {
  char valueLine[24];
  snprintf(valueLine, sizeof(valueLine), "%.1fg Save", value);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, title);
    const char *lines[] = { "Back", "-0.1", "+0.1", valueLine };
    for (uint8_t row = 0; row < 4; row++) {
      const uint8_t y = 25 + row * 12;
      if (row == selected) {
        u8g2.drawStr(0, y, ">");
      }
      u8g2.drawStr(10, y, lines[row]);
    }
  } while (u8g2.nextPage());
}

static inline float grinderApplyDraftStep(float draft, float step, float minValue, float maxValue) {
  return grinderClampDraft(draft + step, minValue, maxValue);
}

static inline float grinderHandleDraftAdjust(const char *title, float draft, float step, float minValue, float maxValue, uint8_t selected) {
  const uint32_t startedAt = millis();
  uint32_t lastStepAt = 0;
  bool held = false;
  while (digitalRead(BUTTON_SQUARE) == LOW) {
    power_off(-1);
    const uint32_t now = millis();
    if (now - startedAt >= 650 && now - lastStepAt >= 250) {
      draft = grinderApplyDraftStep(draft, step * 10.0f, minValue, maxValue);
      grinderDrawNumberEditor(title, draft, selected);
      lastStepAt = now;
      held = true;
    }
    delay(20);
  }
  if (!held) {
    draft = grinderApplyDraftStep(draft, step, minValue, maxValue);
  }
  return draft;
}

static inline bool grinderEditNumber(const char *title, float *output, float minValue, float maxValue) {
  float draft = *output;
  uint8_t selected = 0;
  waitForMenuButtonRelease();
  while (true) {
    power_off(-1);
    grinderDrawNumberEditor(title, draft, selected);
    if (digitalRead(BUTTON_CIRCLE) == LOW) {
      selected = (selected + 1) % 4;
      waitForMenuButtonRelease();
    }
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      if (selected == 0) {
        waitForMenuButtonRelease();
        return false;
      }
      if (selected == 1) {
        draft = grinderHandleDraftAdjust(title, draft, -0.1f, minValue, maxValue, selected);
      } else if (selected == 2) {
        draft = grinderHandleDraftAdjust(title, draft, 0.1f, minValue, maxValue, selected);
      } else {
        *output = draft;
        waitForMenuButtonRelease();
        return true;
      }
    }
    delay(40);
  }
}

void grinderTargetMenu() {
  float value = grinderSettings.targetGrams;
  if (grinderEditNumber("Target g", &value, GRINDER_TARGET_MIN_GRAMS, GRINDER_TARGET_MAX_GRAMS)) {
    grinderSettings.targetGrams = value;
    grinderNormalizeSettings();
    grinderResetAdaptiveSafety();
    grinderSaveSettings();
    grinderSetActionMessage("Target Saved");
  }
}

void grinderSafetyMenu() {
  float value = grinderSettings.safetyMarginGrams;
  if (grinderEditNumber("Safety g", &value, 0.0f, grinderMaxSafetyGrams(grinderSettings.targetGrams))) {
    grinderSettings.safetyMarginGrams = value;
    grinderResetAdaptiveSafety();
    grinderSaveSettings();
    grinderSetActionMessage("Safety Saved");
  }
}

void grinderZeroRangeMenu() {
  float value = grinderSettings.zeroMaxGrams;
  if (grinderEditNumber("Zero +/-g", &value, 0.1f, 20.0f)) {
    grinderSettings.zeroMinGrams = -value;
    grinderSettings.zeroMaxGrams = value;
    grinderSaveSettings();
    grinderSetActionMessage("Zero Saved");
  }
}
#endif

void calibrate() {
  leaveMenu();
  b_calibration = true;
  i_calibration = 0;
}

struct CalibrationRawCapture {
  long raw = 0;
  long firstRaw = 0;
  long secondRaw = 0;
  long spread = 0;
  uint8_t validSamples = 0;
  bool signalTimeout = false;
  bool dataOutOfRange = false;
};

static bool b_calibrationSampleWindowActive = false;
static bool b_calibrationZeroCaptured = false;
static CalibrationRawCapture calibrationZeroCapture;

void calibrationSetStatus(CalibrationRejectReason reason) {
  snprintf(c_calibrationStatus, sizeof(c_calibrationStatus), "%s",
           calibrationRejectReasonText(reason));
  b_calibrationInvalid = reason != CAL_REJECT_NONE;
}

void calibrationResetLastDiagnostics() {
  f_lastCalibrationCandidate = 0.0f;
  f_lastCalibrationVerifiedWeight = 0.0f;
  i_lastCalibrationZeroRaw = 0;
  i_lastCalibrationLoadRaw = 0;
  i_lastCalibrationRawDelta = 0;
  i_lastCalibrationSpread = 0;
}

void calibrationEnsureSampleWindow() {
  if (!b_calibrationSampleWindowActive) {
#if HDS_ENABLE_GRINDER
    if (grinderRuntimeLocksScaleSampling()) {
      Serial.println("Calibration samples locked by grinder");
      return;
    }
#endif
    scale.setSamplesInUse(16);
    b_calibrationSampleWindowActive = true;
  }
}

void calibrationRestoreSampleWindow() {
  if (b_calibrationSampleWindowActive) {
    setScaleSamplesInUseWhenReady(1, "calibration restore");
    b_calibrationSampleWindowActive = false;
  }
}

void calibrationFinish(bool returnToMenu) {
  i_button_cal_status = 0;
  b_calibration = false;
  b_calibrationZeroCaptured = false;
  clearPendingAutomaticTareState();
  consumeScaleTareStatus();
  calibrationRestoreSampleWindow();
  if (returnToMenu) {
    b_menu = true;
    invalidateMenuFrame();
  }
}

void calibrationPrintDiagnostics(CalibrationRejectReason reason,
                                 float previousFactor,
                                 float candidateFactor,
                                 const CalibrationRawCapture *zeroCapture,
                                 const CalibrationRawCapture *loadCapture,
                                 float verifiedWeight) {
  long zeroRaw = zeroCapture != nullptr ? zeroCapture->raw : 0;
  long loadRaw = loadCapture != nullptr ? loadCapture->raw : 0;
  long rawDelta = loadRaw - zeroRaw;
  Serial.print(F("Calibration "));
  Serial.print(reason == CAL_REJECT_NONE ? F("accepted") : F("failed"));
  Serial.print(F(": reason="));
  Serial.print(calibrationRejectReasonText(reason));
  Serial.print(F(" prev="));
  Serial.print(previousFactor, 6);
  Serial.print(F(" candidate="));
  Serial.print(candidateFactor, 6);
  Serial.print(F(" verified_g="));
  Serial.print(verifiedWeight, 4);
  Serial.print(F(" zero_raw="));
  Serial.print(zeroRaw);
  Serial.print(F(" load_raw="));
  Serial.print(loadRaw);
  Serial.print(F(" delta="));
  Serial.print(rawDelta);
  Serial.print(F(" zero_spread="));
  Serial.print(zeroCapture != nullptr ? zeroCapture->spread : 0);
  Serial.print(F(" load_spread="));
  Serial.println(loadCapture != nullptr ? loadCapture->spread : 0);
}

void calibrationRecordDiagnostics(CalibrationRejectReason reason,
                                  float candidateFactor,
                                  const CalibrationRawCapture *zeroCapture,
                                  const CalibrationRawCapture *loadCapture,
                                  float verifiedWeight) {
  calibrationSetStatus(reason);
  f_lastCalibrationCandidate = candidateFactor;
  f_lastCalibrationVerifiedWeight = verifiedWeight;
  i_lastCalibrationZeroRaw = zeroCapture != nullptr ? zeroCapture->raw : 0;
  i_lastCalibrationLoadRaw = loadCapture != nullptr ? loadCapture->raw : 0;
  i_lastCalibrationRawDelta = i_lastCalibrationLoadRaw - i_lastCalibrationZeroRaw;
  long zeroSpread = zeroCapture != nullptr ? zeroCapture->spread : 0;
  long loadSpread = loadCapture != nullptr ? loadCapture->spread : 0;
  i_lastCalibrationSpread = zeroSpread > loadSpread ? zeroSpread : loadSpread;
}

void calibrationShowFailure(CalibrationRejectReason reason) {
  const char *reasonText = calibrationRejectReasonText(reason);
  const char *displayText = calibrationRejectReasonDisplayText(reason);
  Serial.print(F("Calibration failed: "));
  Serial.println(reasonText);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  do {
    u8g2.drawUTF8(AC((char *)"Calibration failed"),
                  u8g2.getMaxCharHeight() + i_margin_top,
                  (char *)"Calibration failed");
    u8g2.drawUTF8(AC((char *)displayText), LCDHeight - i_margin_bottom,
                  (char *)displayText);
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
}

void calibrationShowUsbWarning() {
  Serial.println(F("Calibration warning: USB connected"));
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  u8g2.setFont(FONT_S);
  do {
    u8g2.drawUTF8(AC((char *)"Unplug USB"),
                  u8g2.getMaxCharHeight() + i_margin_top,
                  (char *)"Unplug USB");
    u8g2.drawUTF8(AC((char *)"Cal in 5s"), LCDHeight - i_margin_bottom,
                  (char *)"Cal in 5s");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(5000);
}

long calibrationRawSpread(long firstRaw, long secondRaw) {
  long spread = secondRaw - firstRaw;
  return spread < 0 ? -spread : spread;
}

void calibrationStoreRawCapture(CalibrationRawCapture &capture,
                                const ADS1232DebugInfo &firstInfo,
                                const ADS1232DebugInfo &secondInfo,
                                long spread) {
  capture.firstRaw = firstInfo.smoothedValue;
  capture.secondRaw = secondInfo.smoothedValue;
  capture.spread = spread;
  capture.raw = (capture.firstRaw + capture.secondRaw) / 2;
  capture.validSamples = firstInfo.validSamples < secondInfo.validSamples
                           ? firstInfo.validSamples
                           : secondInfo.validSamples;
  capture.signalTimeout = firstInfo.signalTimeout || secondInfo.signalTimeout;
  capture.dataOutOfRange = firstInfo.dataOutOfRange || secondInfo.dataOutOfRange;
}

bool calibrationCaptureRaw(CalibrationRawCapture &capture,
                           CalibrationRejectReason unstableReason,
                           CalibrationRejectReason &failureReason) {
  calibrationEnsureSampleWindow();
  capture = CalibrationRawCapture();
  unsigned long start = millis();
  unsigned long lastUpdate = start;
  float stabilityLimit = calibrationStabilityRawLimit(f_calibration_value);
  int previousReadIndex = -1;
  bool hasPrevious = false;
  uint8_t stableReads = 0;
  unsigned long stableStartedAt = 0;
  long stableMin = 0;
  long stableMax = 0;
  ADS1232DebugInfo previousInfo;

  while (millis() - start < CALIBRATION_CAPTURE_TIMEOUT_MS) {
    if (scale.update()) {
      lastUpdate = millis();
      ADS1232DebugInfo currentInfo = scale.getDebugInfo();
      if (currentInfo.signalTimeout) {
        capture.signalTimeout = true;
        failureReason = CAL_REJECT_ADC_TIMEOUT;
        return false;
      }
      if (currentInfo.dataOutOfRange) {
        capture.dataOutOfRange = true;
        failureReason = CAL_REJECT_ADC_RANGE;
        return false;
      }
      if (currentInfo.validSamples >= CALIBRATION_MIN_VALID_SAMPLES &&
          currentInfo.readIndex != previousReadIndex) {
        previousReadIndex = currentInfo.readIndex;
        if (hasPrevious) {
          long spread = calibrationRawSpread(previousInfo.smoothedValue,
                                             currentInfo.smoothedValue);
          calibrationStoreRawCapture(capture, previousInfo, currentInfo, spread);
          if ((float)spread <= stabilityLimit) {
            if (stableReads == 0) {
              stableStartedAt = millis();
              stableMin = previousInfo.smoothedValue;
              stableMax = previousInfo.smoothedValue;
            }
            if (currentInfo.smoothedValue < stableMin) {
              stableMin = currentInfo.smoothedValue;
            }
            if (currentInfo.smoothedValue > stableMax) {
              stableMax = currentInfo.smoothedValue;
            }
            stableReads++;
          } else {
            stableReads = 0;
            stableStartedAt = 0;
          }
          long stableWindowSpread = stableMax - stableMin;
          if (stableReads >= CALIBRATION_STABLE_READS_REQUIRED &&
              millis() - stableStartedAt >= CALIBRATION_STABLE_HOLD_MS &&
              (float)stableWindowSpread <= stabilityLimit) {
            failureReason = CAL_REJECT_NONE;
            return true;
          }
        } else {
          capture.raw = currentInfo.smoothedValue;
          capture.validSamples = currentInfo.validSamples;
          hasPrevious = true;
        }
        previousInfo = currentInfo;
      }
    } else if (millis() - lastUpdate > 1500 || scale.getSignalTimeoutFlag()) {
      capture.signalTimeout = true;
      failureReason = CAL_REJECT_ADC_TIMEOUT;
      return false;
    }
    delay(2);
    yield();
  }

  ADS1232DebugInfo info = scale.getDebugInfo();
  failureReason = capture.validSamples < CALIBRATION_MIN_VALID_SAMPLES &&
                          info.validSamples < CALIBRATION_MIN_VALID_SAMPLES
                    ? CAL_REJECT_INSUFFICIENT_SAMPLES
                    : unstableReason;
  return false;
}

void calibrationFail(CalibrationRejectReason reason,
                     float previousFactor,
                     float candidateFactor,
                     const CalibrationRawCapture *zeroCapture,
                     const CalibrationRawCapture *loadCapture,
                     float verifiedWeight) {
  f_calibration_value = previousFactor;
  scale.setCalFactor(f_calibration_value);
  calibrationRecordDiagnostics(reason, candidateFactor, zeroCapture, loadCapture,
                               verifiedWeight);
  calibrationPrintDiagnostics(reason, previousFactor, candidateFactor,
                              zeroCapture, loadCapture, verifiedWeight);
  calibrationShowFailure(reason);
  calibrationFinish(false);
}

void calibrationSave(float previousCalibrationValue,
                     float newCalibrationValue,
                     const CalibrationRawCapture &zeroCapture,
                     const CalibrationRawCapture &loadCapture,
                     float verifiedWeight) {
  f_calibration_value = newCalibrationValue;
  scale.setCalFactor(f_calibration_value);
  storagePutFloat(KEY_CAL1, f_calibration_value);
  calibrationRecordDiagnostics(CAL_REJECT_NONE, f_calibration_value,
                               &zeroCapture, &loadCapture, verifiedWeight);
  calibrationPrintDiagnostics(CAL_REJECT_NONE, previousCalibrationValue,
                              f_calibration_value, &zeroCapture, &loadCapture,
                              verifiedWeight);
}

void calibration(int input) {
  if (b_calibration == true) {
    char c_calval[25];
    if (input == 1) {
      calibrationFail(CAL_REJECT_SMART_CAL_DISABLED, f_calibration_value, 0.0f,
                      nullptr, nullptr, 0.0f);
      return;
    }
    if (i_button_cal_status == 1) {
      if (input == 0) {
        if (!b_calibrationSampleWindowActive) {
          calibrationResetLastDiagnostics();
          b_calibrationZeroCaptured = false;
        }
        calibrationEnsureSampleWindow();
        invalidateEnergyOledFrame();
        u8g2.firstPage();
        do {
          if (b_screenFlipped)
            u8g2.setDisplayRotation(U8G2_R0);
          else
            u8g2.setDisplayRotation(U8G2_R2);
          u8g2.setFontMode(1);
          u8g2.setDrawColor(1);
          u8g2.setFont(FONT_S);
          int x, y;
          x = 0;
          y = u8g2.getMaxCharHeight();
          u8g2.drawUTF8(x, y, "Calibration Weight");
          x += 5;
          y += u8g2.getMaxCharHeight();
          u8g2.drawUTF8(x, y, weights[0]);
          x = 64;
          u8g2.drawUTF8(x, y, weights[3]);
          x = 5;
          y += u8g2.getMaxCharHeight();
          u8g2.drawUTF8(x, y, weights[1]);
          x = 64;
          u8g2.drawUTF8(x, y, weights[4]);
          x = 5;
          y += u8g2.getMaxCharHeight();
          u8g2.drawUTF8(x, y, weights[2]);
          x = 64;
          u8g2.drawUTF8(x, y, weights[5]);
          if (i_cal_weight == 0 || i_cal_weight == 3)
            y = y - u8g2.getMaxCharHeight() * 2;
          if (i_cal_weight == 1 || i_cal_weight == 4)
            y = y - u8g2.getMaxCharHeight();
          if (i_cal_weight == 0 || i_cal_weight == 1 || i_cal_weight == 2)
            x = 0;
          else
            x = 64 - 5;
          int x0 = x;
          int x1 = x;
          int x2 = x0 + 4;
          int y0 = y - u8g2.getMaxCharHeight() + 6;
          int y1 = y;
          int y2 = y - (y1 - y0) / 2;
          u8g2.drawTriangle(x0, y0, x1, y1, x2, y2);

          u8g2.setDrawColor(2);
          drawButton();
        } while (u8g2.nextPage());
      }
      if (input == 1) {
        calibrationEnsureSampleWindow();
        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Remove all weight"),
                        u8g2.getMaxCharHeight() + i_margin_top + 3,
                        (char *)"Remove all weight");
          u8g2.drawUTF8(AC((char *)"to start calibration"),
                        LCDHeight - i_margin_bottom - 3,
                        (char *)"to start calibration");
        } while (u8g2.nextPage());
      }
    }
    if (i_button_cal_status == 2) {
      Serial.println("Before if check, i_cal_weight = " + String(i_cal_weight));

      if (i_cal_weight == 0) {
        calibrationFinish(true);
        return;
      }
      if (b_is_charging) {
        calibrationShowUsbWarning();
      }
      if (input == 0) {
        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Remove weight"),
                        u8g2.getMaxCharHeight() + i_margin_top,
                        (char *)"Remove weight");
          u8g2.drawUTF8(AC((char *)"from scale"), LCDHeight - i_margin_bottom,
                        (char *)"from scale");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(2000);
      }
      invalidateEnergyOledFrame();
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"),
                      u8g2.getMaxCharHeight() + i_margin_top,
                      (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom,
                      (char *)"Wait: 3s");
      } while (u8g2.nextPage());
#ifdef BUZZER
      buzzer.off();
#endif
      delay(1000);
      invalidateEnergyOledFrame();
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"),
                      u8g2.getMaxCharHeight() + i_margin_top,
                      (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom,
                      (char *)"Wait: 2s");
      } while (u8g2.nextPage());
#ifdef BUZZER
      buzzer.off();
#endif
      delay(1000);

      invalidateEnergyOledFrame();
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"),
                      u8g2.getMaxCharHeight() + i_margin_top,
                      (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom,
                      (char *)"Wait: 1s");
      } while (u8g2.nextPage());
#ifdef BUZZER
      buzzer.off();
#endif
      delay(1000);

      invalidateEnergyOledFrame();
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"), AM(),
                      (char *)"Calibrating 0g");
      } while (u8g2.nextPage());

      float previousCalibrationValue = f_calibration_value;
      CalibrationRejectReason zeroFailureReason = CAL_REJECT_NONE;
      if (!calibrationCaptureRaw(calibrationZeroCapture,
                                 CAL_REJECT_UNSTABLE_ZERO,
                                 zeroFailureReason)) {
        calibrationFail(zeroFailureReason, previousCalibrationValue, 0.0f,
                        &calibrationZeroCapture, nullptr, 0.0f);
        return;
      }
      scale.tare();
      consumeScaleTareStatus();
      b_calibrationZeroCaptured = true;
      i_lastCalibrationZeroRaw = calibrationZeroCapture.raw;
      i_lastCalibrationLoadRaw = 0;
      i_lastCalibrationRawDelta = 0;
      i_lastCalibrationSpread = calibrationZeroCapture.spread;
      Serial.print(F("0g raw captured: "));
      Serial.print(calibrationZeroCapture.raw);
      Serial.print(F(" spread="));
      Serial.print(calibrationZeroCapture.spread);
      Serial.print(F(" valid="));
      Serial.println(calibrationZeroCapture.validSamples);
      Serial.println(F("0g calibration done"));
      invalidateEnergyOledFrame();
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"0g calibration done"), AM(),
                      (char *)"0g calibration done");
      } while (u8g2.nextPage());
#ifdef BUZZER
      buzzer.beep(1, BUZZER_DURATION);

      buzzer.off();
#endif
      delay(1000);
      i_button_cal_status++;
    }
    if (i_button_cal_status == 3) {
      if (input == 0) {
        float known_mass = 0;
        scale.update();
        known_mass = weight_values[i_cal_weight];
        char buffer[50];
        snprintf(buffer, sizeof(buffer), "Place %s weight",
                 weights[i_cal_weight]);

        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)),
                        u8g2.getMaxCharHeight() + i_margin_top,
                        (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom,
                        (char *)"Wait: 3s");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(1000);

        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)),
                        u8g2.getMaxCharHeight() + i_margin_top,
                        (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom,
                        (char *)"Wait: 2s");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(1000);

        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)),
                        u8g2.getMaxCharHeight() + i_margin_top,
                        (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom,
                        (char *)"Wait: 1s");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(1000);

        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {

          u8g2.drawUTF8(AC((char *)"Calibrating"), AM(), (char *)"Calibrating");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(1000);
        float previousCalibrationValue = f_calibration_value;
        if (!b_calibrationZeroCaptured) {
          calibrationFail(CAL_REJECT_UNSTABLE_ZERO, previousCalibrationValue,
                          0.0f, nullptr, nullptr, 0.0f);
          return;
        }

        CalibrationRawCapture loadCapture;
        CalibrationRejectReason loadFailureReason = CAL_REJECT_NONE;
        if (!calibrationCaptureRaw(loadCapture, CAL_REJECT_UNSTABLE_LOAD,
                                   loadFailureReason)) {
          calibrationFail(loadFailureReason, previousCalibrationValue, 0.0f,
                          &calibrationZeroCapture, &loadCapture, 0.0f);
          return;
        }

        long rawDelta = loadCapture.raw - calibrationZeroCapture.raw;
        float candidateCalibrationValue = (float)rawDelta / known_mass;
        CalibrationRejectReason validationReason =
          validateCalibrationCandidateBasics(known_mass, rawDelta,
                                             candidateCalibrationValue);
        if (validationReason != CAL_REJECT_NONE) {
          calibrationFail(validationReason, previousCalibrationValue,
                          candidateCalibrationValue, &calibrationZeroCapture,
                          &loadCapture, 0.0f);
          return;
        }

        scale.setCalFactor(candidateCalibrationValue);
        float verifiedWeight = scale.getData();
        validationReason = validateCalibrationVerification(known_mass,
                                                           verifiedWeight);
        if (validationReason != CAL_REJECT_NONE) {
          calibrationFail(validationReason, previousCalibrationValue,
                          candidateCalibrationValue, &calibrationZeroCapture,
                          &loadCapture, verifiedWeight);
          return;
        }

        calibrationSave(previousCalibrationValue, candidateCalibrationValue,
                        calibrationZeroCapture, loadCapture, verifiedWeight);
        Serial.print(F("New calibration value f: "));
        Serial.println(f_calibration_value);
        formatFloatSafe(c_calval, sizeof(c_calval), f_calibration_value, 2);
        Serial.print(F("New calibration value c: "));
        Serial.println(trim(c_calval));

        invalidateEnergyOledFrame();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Recalibration done"), AM(),
                        (char *)"Recalibration done");
        } while (u8g2.nextPage());
#ifdef BUZZER
        buzzer.off();
#endif
        delay(1000);
#ifdef BUZZER
        buzzer.beep(1, BUZZER_DURATION);
        buzzer.off();
#endif
        delay(1000);
        calibrationFinish(false);
        return;
      }
    }
  }
}

#if HDS_FEATURE_PULL_OTA
void wifiUpdate(const PullOtaTargetVersion &target) {
  if (b_softSleep) {
    wakeScaleFromSoftSleep("WiFi OTA wake");
  }
#if HDS_ENABLE_ENERGY_MENU
  recordEnergyActivity();
#endif
#ifdef BUZZER
  buzzer.off();
#endif
  pullOtaUpdate(target);
  leaveMenu();
}

void wifiUpdate() {
  wifiUpdate(pullOtaNoTargetVersion());
}
#endif

void showAbout() {
  actionMessage = FIRMWARE_VER;
  actionMessage2 = LINE3;
  b_showAbout = true;
  u8g2.setFont(FONT_S);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.setFont(FONT_S);
    u8g2.drawStr(AC(actionMessage.c_str()), AM() - 24, actionMessage.c_str());
    u8g2.drawStr(AC(actionMessage2.c_str()), AM(), actionMessage2.c_str());
    u8g2.drawStr(AC(GIT_REV), AM()+ 24, GIT_REV);
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  while (b_showAbout) {
    if (digitalRead(BUTTON_SQUARE) == LOW)
      b_showAbout = false;
  }
  waitForMenuButtonRelease();
}

void showLogo() {
  b_showLogo = true;
  invalidateEnergyOledFrame();
  u8g2.firstPage();

  do {
    if (!b_showNumber) {
      u8g2.setFont(u8g2_font_logisoso22_tf);
      u8g2.drawStr(AC("Half"), 26, "Half");
      u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
      u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
    } else {
      u8g2.drawXBM(121, 52, 7, 12, image_battery_4);
      u8g2.drawXBM(3, 51, 5, 13, image_ble_enabled);
      u8g2.setFont(FONT_TIMER);
      u8g2.drawStr(AC("345"), LCDHeight - 8, "345");

      float number = 1234.5;
      int i_weightInt = (int)number;
      if (number >= 0) {
        b_negativeWeight = false;
      } else {
        b_negativeWeight = true;
      }

      float decimalPart = number - (float)(i_weightInt);
      int i_weightFirstDecimal = abs((int)(decimalPart * 10));
      char integerStr[10] =
        "-0";
      char decimalStr[10] = "0";
      if (number >= 0 || number <= -1) {
        snprintf(integerStr, sizeof(integerStr), "%d", i_weightInt);
      }
      snprintf(decimalStr, sizeof(decimalStr), "%d", i_weightFirstDecimal);
      u8g2.setFont(FONT_GRAM);
      int gramWidth = u8g2.getUTF8Width("g");
      u8g2.setFont(FONT_WEIGHT);
      int integerWidth = u8g2.getUTF8Width(trim(integerStr));
      int decimalWidth = u8g2.getUTF8Width(trim(decimalStr));
      int decimalPointWidth = u8g2.getUTF8Width(".");
      if (number <= -1000.0)
        gramWidth = 0;
      int x_integer = (128 - (integerWidth + decimalWidth + gramWidth + decimalPointWidth - 6 - 1)) / 2;
      int x_decimalPoint = x_integer + integerWidth - 4;
      int x_decimal = x_decimalPoint + decimalPointWidth - 4;
      int x_gram = x_decimal + decimalWidth - 1;
      int y = AT() - 15;
      u8g2.drawStr(x_decimalPoint, y, ".");
      u8g2.drawStr(x_integer, y,
                   trim(integerStr));
      u8g2.drawStr(x_decimal, y, trim(decimalStr));
      if (number > -1000.0) {
        u8g2.setFont(FONT_GRAM);
        u8g2.drawStr(x_gram, y - 5, "g");
      }
    }
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  while (b_showLogo) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      b_showNumber = !b_showNumber;
      b_showLogo = false;
    }
  }
  waitForMenuButtonRelease();
}

void enableDebug() {
  u8g2.setFont(FONT_M);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"Exit Menu"), AM(), (char *)"Exit Menu");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  b_debug = true;
  leaveMenu();
}

void calibrateVoltage() {
  actionMessage = "Calibrate 4.2v";
  menuActionMessageChanged();
  t_actionMessageDelay = 1000;
  const int numReadings = 50;
  long adcSum = 0;

  for (int i = 0; i < numReadings; i++) {
    adcSum += analogRead(BATTERY_PIN);
    delay(10);
  }

  float adcValue = adcSum / (float)numReadings;

  float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;
  float batteryVoltage = voltageAtPin * dividerRatio;

  f_batteryCalibrationFactor = 4.2 / batteryVoltage;

  storagePutFloat(KEY_BAT_CAL, f_batteryCalibrationFactor);

  Serial.print("Battery Voltage Factor set to: ");
  Serial.println(f_batteryCalibrationFactor);
}

void refreshMenuRows() {
  updateToggleLabel(menuHeartbeatLabel, b_requireHeartBeat);
  updateToggleLabel(menuBleButtonsLabel, b_btnFuncWhileConnected);
  updateToggleLabel(menuFlipScreenLabel, b_screenFlipped);
  updateToggleLabel(menuAutoSleepLabel, b_autoSleep);
  updateToggleLabel(menuQuickBootLabel, b_quickBoot);
  updateToggleLabel(menuTapTareLabel, b_tapTareEnabled);
  updateToggleLabel(menuTapTimerLabel, b_tapTimerEnabled);
  snprintf(menuTimeOnTopLabel, sizeof(menuTimeOnTopLabel), "Top: %s",
           b_timeOnTop ? "Time" : "Weight");
  if (fabsf(f_maxDriftCompensation) < 0.0001f) {
    snprintf(menuDriftLabel, sizeof(menuDriftLabel), "Drift: Off");
  } else if (fabsf(f_maxDriftCompensation - 0.075f) < 0.0001f) {
    snprintf(menuDriftLabel, sizeof(menuDriftLabel), "Drift: 0.075g");
  } else {
    snprintf(menuDriftLabel, sizeof(menuDriftLabel), "Drift: %.2fg",
             f_maxDriftCompensation);
  }
#ifdef BUZZER
  updateToggleLabel(menuBuzzerLabel, b_beep);
#endif
#if HDS_FEATURE_WIFI
  updateToggleLabel(menuWifiLabel, b_wifiOnBoot);
#endif
#if HDS_ENABLE_GRINDER
  updateToggleLabel(menuGrinderEnabledLabel, grinderSettings.enabled);
  snprintf(menuGrinderTargetLabel, sizeof(menuGrinderTargetLabel),
           "Target: %.1fg", grinderSettings.targetGrams);
  snprintf(menuGrinderSafetyLabel, sizeof(menuGrinderSafetyLabel),
           "Safety: %.1fg", grinderSettings.safetyMarginGrams);
  snprintf(menuGrinderZeroLabel, sizeof(menuGrinderZeroLabel),
           "Zero Range: %.1fg", grinderSettings.zeroMaxGrams);
#endif
#if HDS_ENABLE_ENERGY_MENU
  refreshEnergyMenuRows();
#endif
}

void navigateMenu(int direction) {
  recordEnergyActivity();
  currentIndex = (currentIndex + direction + currentMenuSize) % currentMenuSize;
  currentSelection = currentMenu[currentIndex];
  invalidateMenuFrame();
  Serial.print("currentIndex ");
  Serial.println(currentIndex);
}

void backMenu() {
  if (currentMenu == mainMenu) {
    exitMenu();
    return;
  }
#if HDS_ENABLE_GRINDER
  if (currentMenu == grinderMenu && b_grinderMenuDirectEntry) {
    exitMenu();
    return;
  }
#endif
  const Menu *origin = currentSelection->parentMenu;
  currentMenu = mainMenu;
  currentMenuSize = getMenuSize(mainMenu);
  currentIndex = 0;
  for (int index = 0; index < currentMenuSize; ++index) {
    if (currentMenu[index] == origin) currentIndex = index;
  }
  currentSelection = currentMenu[currentIndex];
  invalidateMenuFrame();
}

void selectMenu() {
#if HDS_ENABLE_ENERGY_MENU
  recordEnergyActivity();
#endif
  invalidateMenuFrame();
  if (currentSelection->subMenu) {
    if (currentSelection == &menuScale) {
      currentMenu = scaleMenu;
      currentMenuSize = getMenuSize(scaleMenu);
    } else if (currentSelection == &menuConnections) {
      currentMenu = connectionsMenu;
      currentMenuSize = getMenuSize(connectionsMenu);
    } else if (currentSelection == &menuDisplay) {
      currentMenu = displayMenu;
      currentMenuSize = getMenuSize(displayMenu);
    } else if (currentSelection == &menuPower) {
      currentMenu = powerMenu;
      currentMenuSize = getMenuSize(powerMenu);
    } else if (currentSelection == &menuInfo) {
      currentMenu = infoMenu;
      currentMenuSize = getMenuSize(infoMenu);
#if HDS_ENABLE_GRINDER
    } else if (currentSelection == &menuGrinder) {
      b_grinderMenuDirectEntry = false;
      currentMenu = grinderMenu;
      currentMenuSize = getMenuSize(grinderMenu);
#endif
    }
    currentIndex = 0;
    currentSelection = currentMenu[currentIndex];
  } else if (currentSelection->action) {
    currentSelection->action();
  } else if (currentSelection->parentMenu) {
    backMenu();
  }
}

void showMenu() {
  const unsigned long now = millis();
  if (!menuFrameNeedsRender(now)) return;
  refreshMenuRows();
  const bool actionMessageVisible = now - t_actionMessage < t_actionMessageDelay;
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);

  u8g2.setFont(FONT_S);
  invalidateEnergyOledFrame();
  u8g2.firstPage();
  do {
    if (actionMessageVisible) {
      u8g2.setFont(FONT_M);
      if (AC(actionMessage.c_str()) < 0)
        u8g2.setFont(FONT_S);
      if (actionMessage2 == "Default")
        u8g2.drawStr(AC(actionMessage.c_str()), AM(), actionMessage.c_str());
      else {
        u8g2.drawStr(AC(actionMessage.c_str()), AM() - 12,
                     actionMessage.c_str());
        u8g2.drawStr(AC(actionMessage2.c_str()), AM() + 12,
                     actionMessage2.c_str());
      }
    } else {
      actionMessage == "Default";
      currentPage = currentIndex / linesPerPage + 1;
      totalPages = (currentMenuSize + linesPerPage - 1) / linesPerPage;
      char pageInfo[10];
      snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
      if (totalPages > 1)
        u8g2.drawStr(AR(pageInfo), u8g2.getMaxCharHeight(),
                     pageInfo);
      for (int i = 0; i < currentMenuSize; i++) {
        if (currentMenu[i] == currentSelection) {
          u8g2.drawStr(0, u8g2.getMaxCharHeight() * (i % linesPerPage + 1),
                       ">");
        }
        if (i >= (currentPage - 1) * linesPerPage && i < currentPage * linesPerPage)
          u8g2.drawStr(10, u8g2.getMaxCharHeight() * (i % linesPerPage + 1),
                       currentMenu[i]->name);
      }
    }
  } while (u8g2.nextPage());
  lastMenuFrameRender = now;
  menuFrameShowsActionMessage = actionMessageVisible;
  menuFrameDirty = false;
}

#endif
