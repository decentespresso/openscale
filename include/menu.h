#ifndef MENU_H
#define MENU_H

#include "esp32-hal.h"
#include "parameter.h"
#include "wifi_setup.h"
#include "grinder_runtime.h"
const char *weights[] = { "Exit", "50g", "100g", "200g", "500g", "1000g" };
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
template<typename T> int getMenuSize(T &menu) {
  return sizeof(menu) / sizeof(menu[0]);
}

// Menu structure
struct Menu {
  const char *name;  // menu name
  void (*action)();  // what to do NULL for submenu
  Menu *subMenu;     // submenu NULL for none
  Menu *parentMenu;  // parentmenu NULL for root menu
};

// Function prototypes
void exitMenu();
#ifdef BUZZER
void buzzerOn();
void buzzerOff();
#endif
void toggleWifiOff();
void toggleWifiOn();
void resetWifi();
void showWifiStatus();
void heartbeatOn();
void heartbeatOff();
void calibrate();
void drawButton();
void wifiUpdate();
void showStatus();
void showAbout();
void showMenu();
void showLogo();
void calibrateVoltage();
void navigateMenu(int direction);
void selectMenu();
void enableDebug();
void flipScreenOn();
void flipScreenOff();
void timeOnTopOn();
void timeOnTopOff();
void btnFuncWhileConnectedOn();
void btnFuncWhileConnectedOff();
void autoSleepOn();
void autoSleepOff();
void quickBootOn();
void quickBootOff();
void driftCompOff();
void driftComp0050();
void driftComp0075();
void driftComp0100();
void driftComp0200();
void grinderOn();
void grinderOff();
void grinderSelectPlugMenu();
void grinderTargetMenu();
void grinderSafetyMenu();
void grinderZeroRangeMenu();
void wifi_init();


// Top-level menu options
// 1/5 define the 1st level menu
Menu menuExit = { "Exit", exitMenu, NULL, NULL };
#ifdef BUZZER
Menu menuBuzzer = { "Buzzer", NULL, NULL, NULL };
#endif
Menu menuCalibration = { "Calibration", NULL, NULL, NULL };
Menu menuWifi = { "WiFi Settings", NULL, NULL, NULL };
Menu menuStatus = { "Status", showStatus, NULL, NULL };
// Menu menuWiFiUpdate = {"WiFi Update", NULL, NULL, NULL};
Menu menuAbout = { "About", showAbout, NULL, NULL };
Menu menuLogo = { "Show Logo", showLogo, NULL, NULL };
Menu menuFactory = { "Factory", NULL, NULL, NULL };
Menu menuHeartbeat = { "Heartbeat", NULL, NULL, NULL };
Menu menuFlipScreen = { "Flip Screen", NULL, NULL, NULL };
Menu menuTimeOnTop = { "Time On Top", NULL, NULL, NULL };
Menu menuBtnFuncWhileConnected = { "Button with BLE", NULL, NULL, NULL };
Menu menuAutoSleep = { "Auto Sleep", NULL, NULL, NULL };
Menu menuQuickBoot = { "Quick Boot", NULL, NULL, NULL };
Menu menuDriftComp = { "Drift Comp", NULL, NULL, NULL };
Menu menuGrinder = { "Grinder Plug", NULL, NULL, NULL };


// 2/5 define the 2st level menu
#ifdef BUZZER
// Buzzer submenu
Menu menuBuzzerBack = { "Back", NULL, NULL, &menuBuzzer };
Menu menuBuzzerOn = { "Buzzer On", buzzerOn, NULL, &menuBuzzer };
Menu menuBuzzerOff = { "Buzzer Off", buzzerOff, NULL, &menuBuzzer };
Menu *buzzerMenu[] = { &menuBuzzerBack, &menuBuzzerOn, &menuBuzzerOff };
#endif
// Calibration submenu
Menu menuCalibrationBack = { "Back", NULL, NULL, &menuCalibration };
Menu menuCalibrate = { "Calibrate", calibrate, NULL, &menuCalibration };
Menu *calibrationMenu[] = { &menuCalibrationBack, &menuCalibrate };

// WiFi submenu
Menu menuWiFiUpdateBack = { "Back", NULL, NULL, &menuWifi };
Menu menuWiFiOnOption = { "WiFi On", toggleWifiOn, NULL, &menuWifi };
Menu menuWiFiOffOption = { "WiFi Off", toggleWifiOff, NULL, &menuWifi };
Menu menuWiFiStatusOption = { "WiFi Status", showWifiStatus, NULL, &menuWifi };
Menu menuWiFiResetOption = { "Reset WiFi", resetWifi, NULL, &menuWifi };

Menu *wifiUpdateMenu[] = {
  &menuWiFiUpdateBack,
  &menuWiFiOnOption,
  &menuWiFiOffOption,
  &menuWiFiStatusOption,
  &menuWiFiResetOption,
};

// Heartbeat detection
Menu menuHeartbeatBack = { "Back", NULL, NULL, &menuHeartbeat };
Menu menuHeartbeatOn = { "Heartbeat On", heartbeatOn, NULL, &menuHeartbeat };
Menu menuHeartbeatOff = { "Heartbeat Off", heartbeatOff, NULL, &menuHeartbeat };
Menu *heartbeatMenu[] = { &menuHeartbeatBack, &menuHeartbeatOn,
                          &menuHeartbeatOff };

// Screen flip option
Menu menuFlipScreenBack = { "Back", NULL, NULL, &menuFlipScreen };
Menu menuFlipScreenOn = { "Flip On", flipScreenOn, NULL, &menuFlipScreen };
Menu menuFlipScreenOff = { "Flip Off", flipScreenOff, NULL, &menuFlipScreen };
Menu *flipScreenMenu[] = { &menuFlipScreenBack, &menuFlipScreenOn,
                           &menuFlipScreenOff };

// Swap weight time option
Menu menuTimeOnTopBack = { "Back", NULL, NULL, &menuTimeOnTop };
Menu menuTimeOnTopOn = { "Time On Top", timeOnTopOn, NULL, &menuTimeOnTop };
Menu menuTimeOnTopOff = { "Weight On Top", timeOnTopOff, NULL, &menuTimeOnTop };
Menu *timeOnTopMenu[] = { &menuTimeOnTopBack, &menuTimeOnTopOn,
                          &menuTimeOnTopOff };

// Enable button function while BLE connected
Menu menuBtnFuncWhileConnectedBack = { "Back", NULL, NULL,
                                       &menuBtnFuncWhileConnected };
Menu menuBtnFuncWhileConnectedOn = { "Enable Buttons", btnFuncWhileConnectedOn,
                                     NULL, &menuBtnFuncWhileConnected };
Menu menuBtnFuncWhileConnectedOff = { "Disable Buttons",
                                      btnFuncWhileConnectedOff, NULL,
                                      &menuBtnFuncWhileConnected };
Menu *btnFuncWhileConnectedMenu[] = { &menuBtnFuncWhileConnectedBack,
                                      &menuBtnFuncWhileConnectedOn,
                                      &menuBtnFuncWhileConnectedOff };

// Auto sleep function
Menu menuAutoSleepBack = { "Back", NULL, NULL, &menuAutoSleep };
Menu menuAutoSleepOn = { "Auto Sleep On", autoSleepOn, NULL, &menuAutoSleep };
Menu menuAutoSleepOff = { "Auto Sleep Off", autoSleepOff, NULL, &menuAutoSleep };
Menu *autoSleepMenu[] = { &menuAutoSleepBack, &menuAutoSleepOn, &menuAutoSleepOff };

// Quick boot function(aka no delay when pressing the button to boot the scale)
Menu menuQuickBootBack = { "Back", NULL, NULL, &menuQuickBoot };
Menu menuQuickBootOn = { "Quick Boot On", quickBootOn, NULL, &menuQuickBoot };
Menu menuQuickBootOff = { "Quick Boot Off", quickBootOff, NULL, &menuQuickBoot };
Menu *quickBootMenu[] = { &menuQuickBootBack, &menuQuickBootOn, &menuQuickBootOff };

Menu menuDriftCompBack = { "Back", NULL, NULL, &menuDriftComp };
Menu menuDriftCompOff = { "Drift Comp Off", driftCompOff, NULL, &menuDriftComp };
Menu menuQuickBoot0050 = { "0.05g", driftComp0050, NULL, &menuDriftComp };
Menu menuQuickBoot0075 = { "0.075g", driftComp0075, NULL, &menuDriftComp };
Menu menuQuickBoot0100 = { "0.1g", driftComp0100, NULL, &menuDriftComp };
Menu menuQuickBoot0200 = { "0.2g", driftComp0200, NULL, &menuDriftComp };
Menu *driftCompMenu[] = { &menuDriftCompBack, &menuDriftCompOff, &menuQuickBoot0050, &menuQuickBoot0075, &menuQuickBoot0100, &menuQuickBoot0200 };

Menu menuGrinderBack = { "Back", NULL, NULL, &menuGrinder };
Menu menuGrinderOn = { "Grinder On", grinderOn, NULL, &menuGrinder };
Menu menuGrinderOff = { "Grinder Off", grinderOff, NULL, &menuGrinder };
Menu menuGrinderSelect = { "Select Plug", grinderSelectPlugMenu, NULL, &menuGrinder };
Menu menuGrinderTarget = { "Target g", grinderTargetMenu, NULL, &menuGrinder };
Menu menuGrinderSafety = { "Safety g", grinderSafetyMenu, NULL, &menuGrinder };
Menu menuGrinderZero = { "Zero Range", grinderZeroRangeMenu, NULL, &menuGrinder };
Menu *grinderMenu[] = { &menuGrinderBack, &menuGrinderOn, &menuGrinderOff, &menuGrinderSelect, &menuGrinderTarget, &menuGrinderSafety, &menuGrinderZero };

// Menu menuFactoryBack = { "Back", NULL, NULL, &menuFactory };
// Menu menuCalibrateVoltage = { "Calibrate 4.2v", calibrateVoltage, NULL,
// &menuFactory }; Menu menuFactoryDebug = { "Debug Info", enableDebug, NULL,
// &menuFactory }; Menu *factoryMenu[] = { &menuFactoryBack,
// &menuCalibrateVoltage, &menuFactoryDebug };

// Main menu
// 3/5 write all the 1st menu to mainMenu
Menu *mainMenu[] = {
  &menuExit,
#ifdef BUZZER
  &menuBuzzer,
#endif
  &menuCalibration, &menuWifi, &menuStatus,
  // &menuWiFiUpdate,
  &menuAbout, &menuLogo, &menuHeartbeat, &menuFlipScreen, &menuTimeOnTop,
  &menuBtnFuncWhileConnected, &menuAutoSleep, &menuQuickBoot, &menuDriftComp,
  &menuGrinder,
  //, &menuFactory
};
//  &menuHolder1, &menuHolder2, &menuHolder3, &menuHolder4,
//  &menuHolder5, &menuHolder6};
Menu **currentMenu = mainMenu;
Menu *currentSelection = mainMenu[0];
int currentMenuSize = getMenuSize(mainMenu);  // Top-level menu size
int currentIndex = 0;
const int linesPerPage =
  4;                                                  // Maximum number of lines that can fit on the display
int currentPage = 0;                                  // Determine the current page
int totalPages = currentMenuSize / linesPerPage + 1;  // Calculate total pages

// 4/5 link all the submenus
void linkSubmenus() {
// Link submenus
#ifdef BUZZER
  menuBuzzer.subMenu = buzzerMenu[0];
#endif
  menuCalibration.subMenu = calibrationMenu[0];
  menuWifi.subMenu = wifiUpdateMenu[0];
  menuHeartbeat.subMenu = heartbeatMenu[0];
  menuFlipScreen.subMenu = flipScreenMenu[0];
  menuTimeOnTop.subMenu = timeOnTopMenu[0];
  menuBtnFuncWhileConnected.subMenu = btnFuncWhileConnectedMenu[0];
  menuAutoSleep.subMenu = autoSleepMenu[0];
  menuQuickBoot.subMenu = quickBootMenu[0];
  menuDriftComp.subMenu = driftCompMenu[0];
  menuGrinder.subMenu = grinderMenu[0];
  // menuFactory.subMenu = factoryMenu[0];
}

// Menu actions
void exitMenu() {
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"Exit Menu"), AM(), (char *)"Exit Menu");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  grinderResumeAfterMenu();
  b_menu = false;
  b_grinderMenuDirectEntry = false;
  currentMenu = mainMenu;
  currentMenuSize = getMenuSize(mainMenu);
  currentIndex = 0;
  currentSelection = currentMenu[currentIndex];
  // Optionally reset or perform an exit action
  t_menuExitTime = millis();
  // Capture the moment when menu exit begins to establish a reference point
  // This enables timing-based protection against unintended triggers
}

#ifdef BUZZER
void buzzerOn() {
  if (b_beep == false) {
    b_beep = true;
    buzzer.beep(1, BUZZER_DURATION);
  }
  actionMessage = "Buzzer on";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutInt(KEY_BEEP, b_beep);
  Serial.println("Buzzer On stored in NVS.");
}

void buzzerOff() {
  b_beep = false;
  actionMessage = "Buzzer off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutInt(KEY_BEEP, b_beep);
  Serial.println("Buzzer off stored in NVS.");
}
#endif

void toggleWifiOn() {
  b_wifiOnBoot = true;
  if (grinderSettings.enabled) {
    grinderSettings.previousWifiOnBoot = true;
    grinderSettings.previousWifiOnBootSaved = true;
    grinderSaveSettings();
  }
  actionMessage = "WiFi Enabled";
  actionMessage2 = "Restart scale";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_WIFI_BOOT, true);
  Serial.printf("%s\n", actionMessage);
}

void toggleWifiOff() {
  if (grinderSettings.enabled) {
    grinderSettings.previousWifiOnBoot = false;
    grinderSettings.previousWifiOnBootSaved = true;
    grinderSaveSettings();
    actionMessage = "Grinder Needs WiFi";
    actionMessage2 = "Off After Grinder";
    t_actionMessage = millis();
    t_actionMessageDelay = 1500;
    Serial.println("WiFi Off deferred until Grinder Off.");
    return;
  }
  b_wifiOnBoot = false;
  actionMessage = "WiFi Disabled";
  actionMessage2 = "Restart scale";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_WIFI_BOOT, false);
  Serial.printf("%s\n", actionMessage);
}

void showWifiStatus() {
  b_showWifiData = true;

  // Get WiFi info
  String ssid = WiFi.isConnected() ? WiFi.SSID() : "N/A";
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : "0.0.0.0";
  const char *status = WiFi.isConnected() ? "Enabled" : "Disabled";

  u8g2.firstPage();
  do {

    u8g2.setFont(u8g2_font_6x12_tr);  // Use readable font

    u8g2.drawStr(0, 12, "WiFi Status:");
    u8g2.drawStr(72, 12, status);

    u8g2.drawStr(0, 28, "SSID:");
    u8g2.drawStr(40, 28, ssid.c_str());

    u8g2.drawStr(0, 44, "IP:");
    u8g2.drawStr(40, 44, ip.c_str());

  } while (u8g2.nextPage());
  delay(1000);
  while (b_showWifiData) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      b_showWifiData = false;
    }
  }
}

void showStatus() {
  b_showStatusData = true;

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

  char wifiLine[32];
  char bleLine[32];
  char sleepLine[32];
  char driftLine[32];
  char grinderLine[32];
  snprintf(wifiLine, sizeof(wifiLine), "WiFi:%s %s %s",
           b_wifiOnBoot ? "On" : "Off",
           wifiCredentialsSaved() ? "Saved" : "NoCred",
           wifiRunState);
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
  snprintf(grinderLine, sizeof(grinderLine), "Gr:%s %.1fg",
           grinderRuntime.status[0] ? grinderRuntime.status : grinderStateText(grinderRuntime.state),
           grinderSettings.targetGrams);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(0, 10, "Status");
    u8g2.drawStr(0, 22, wifiLine);
    u8g2.drawStr(0, 34, bleLine);
    u8g2.drawStr(0, 46, sleepLine);
    u8g2.drawStr(0, 58, grinderSettings.enabled ? grinderLine : driftLine);
  } while (u8g2.nextPage());
  delay(1000);
  while (b_showStatusData) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      b_showStatusData = false;
    }
  }
}

void resetWifi() {
  saveCredentials("", "");
  actionMessage = "WiFi Reset";
  actionMessage2 = "Restart scale";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
}

void heartbeatOn() {
  b_requireHeartBeat = true;
  actionMessage = "Heartbeat On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_HEARTBEAT, b_requireHeartBeat);
  Serial.println("Heartbeat detection...On");
}

void heartbeatOff() {
  b_requireHeartBeat = false;
  actionMessage = "Heartbeat Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_HEARTBEAT, b_requireHeartBeat);
  Serial.println("Heartbeat detection...Off");
}

void flipScreenOn() {
  b_screenFlipped = true;
  actionMessage = "Flip On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_SCREEN_FLIP, b_screenFlipped);
  u8g2.setDisplayRotation(U8G2_R0);
  Serial.println("Screen flipped...On");
}

void flipScreenOff() {
  b_screenFlipped = false;
  actionMessage = "Flip Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_SCREEN_FLIP, b_screenFlipped);
  u8g2.setDisplayRotation(U8G2_R2);
  Serial.println("Screen flipped...Off");
}

void timeOnTopOn() {
  b_timeOnTop = true;
  actionMessage = "Time On Top";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_TIME_ON_TOP, b_timeOnTop);
  Serial.println("Time On Top");
}

void timeOnTopOff() {
  b_timeOnTop = false;
  actionMessage = "Weight On Top";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_TIME_ON_TOP, b_timeOnTop);
  Serial.println("Weight On Top");
}

void btnFuncWhileConnectedOn() {
  b_btnFuncWhileConnected = true;
  actionMessage = "BLE Btns On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_BTN_CONN, b_btnFuncWhileConnected);
  Serial.println("BLE Btns On");
}

void btnFuncWhileConnectedOff() {
  b_btnFuncWhileConnected = false;
  actionMessage = "BLE Btns Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_BTN_CONN, b_btnFuncWhileConnected);
  Serial.println("BLE Btns Off");
}

void autoSleepOn() {
  b_autoSleep = true;
  actionMessage = "Autosleep On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_AUTO_SLEEP, b_autoSleep);
  Serial.println("Autosleep on stored in NVS.");
}

void autoSleepOff() {
  b_autoSleep = false;
  actionMessage = "Autosleep Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_AUTO_SLEEP, b_autoSleep);
  Serial.println("Autosleep off stored in NVS.");
}

void quickBootOn() {
  b_quickBoot = true;
  actionMessage = "Quick Boot On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_QUICK_BOOT, b_quickBoot);
  Serial.println("Quick boot on stored in NVS.");
}

void quickBootOff() {
  b_quickBoot = false;
  actionMessage = "Quick Boot Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutBool(KEY_QUICK_BOOT, b_quickBoot);
  Serial.println("Quick boot off stored in NVS.");
}

void driftCompOff() {
  f_maxDriftCompensation = 0.0;
  actionMessage = "Drift Comp Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  Serial.println("Drift Comp Off stored in NVS.");
}

void driftComp0050() {
  f_maxDriftCompensation = 0.05;
  actionMessage = "Drift Comp 0.05g";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  Serial.println("Drift Comp 0.05g stored in NVS.");
}

void driftComp0075() {
  f_maxDriftCompensation = 0.075;
  actionMessage = "Drift Comp 0.075g";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  Serial.println("Drift Comp 0.075g stored in NVS.");
}

void driftComp0100() {
  f_maxDriftCompensation = 0.1;
  actionMessage = "Drift Comp 0.1g";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  Serial.println("Drift Comp 0.1g stored in NVS.");
}

void driftComp0200() {
  f_maxDriftCompensation = 0.2;
  actionMessage = "Drift Comp 0.2g";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  storagePutFloat(KEY_DRIFT_MAX, f_maxDriftCompensation);
  Serial.println("Drift Comp 0.2g stored in NVS.");
}

void grinderSetActionMessage(const char *line1, const char *line2 = nullptr) {
  actionMessage = line1;
  actionMessage2 = line2 == nullptr ? "Default" : line2;
  t_actionMessage = millis();
  t_actionMessageDelay = 1500;
}

void grinderOn() {
  if (!grinderSettings.previousWifiOnBootSaved) {
    grinderSettings.previousWifiOnBoot = b_wifiOnBoot;
    grinderSettings.previousWifiOnBootSaved = true;
  }
  grinderSetEnabled(true);
  b_wifiOnBoot = true;
  EEPROM.put(i_addr_enableWifiOnBoot, true);
  EEPROM.commit();
  if (!b_wifiEnabled) {
    wifi_init();
  }
  grinderSetActionMessage("Grinder On", "WiFi On");
  Serial.printf("[grinder] enabled selected=%s ip=%s\n",
                grinderSettings.selectedMac,
                grinderSettings.lastIp.toString().c_str());
}

void grinderOff() {
  const bool restoreWifiOnBoot = grinderSettings.previousWifiOnBoot;
  const bool restoreWifiOnBootSaved = grinderSettings.previousWifiOnBootSaved;
  grinderSetEnabled(false);
  if (restoreWifiOnBootSaved) {
    b_wifiOnBoot = restoreWifiOnBoot;
    EEPROM.put(i_addr_enableWifiOnBoot, restoreWifiOnBoot);
    EEPROM.commit();
    grinderSettings.previousWifiOnBoot = false;
    grinderSettings.previousWifiOnBootSaved = false;
    grinderSaveSettings();
  }
  grinderSetActionMessage("Grinder Off");
  Serial.println("Grinder Off stored in NVS.");
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

void grinderWaitForButtonRelease() {
  while (digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW) {
    delay(20);
  }
}

void grinderDrawPlugList(uint8_t selected) {
  const uint8_t total = grinderRuntime.discoveredCount + 1;
  const uint8_t rows = 6;
  uint8_t first = (selected / rows) * rows;
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
  grinderWaitForButtonRelease();
  uint8_t selected = 0;
  bool selecting = true;
  while (selecting) {
    power_off(-1);
    grinderDrawPlugList(selected);
    if (digitalRead(BUTTON_CIRCLE) == LOW) {
      selected = (selected + 1) % (grinderRuntime.discoveredCount + 1);
      grinderWaitForButtonRelease();
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
        grinderSetActionMessage("Plug Selected", grinderSettings.enabled ? "Saved" : "Turn Grinder On");
        selecting = false;
      }
      grinderWaitForButtonRelease();
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
  grinderWaitForButtonRelease();
  while (true) {
    power_off(-1);
    grinderDrawNumberEditor(title, draft, selected);
    if (digitalRead(BUTTON_CIRCLE) == LOW) {
      selected = (selected + 1) % 4;
      grinderWaitForButtonRelease();
    }
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      if (selected == 0) {
        grinderWaitForButtonRelease();
        return false;
      }
      if (selected == 1) {
        draft = grinderHandleDraftAdjust(title, draft, -0.1f, minValue, maxValue, selected);
      } else if (selected == 2) {
        draft = grinderHandleDraftAdjust(title, draft, 0.1f, minValue, maxValue, selected);
      } else {
        *output = draft;
        grinderWaitForButtonRelease();
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

void calibrate() {
  b_menu = false;
  b_calibration = true;  // 让按钮进入校准状态3
  i_calibration = 0;
  // calibration(0);
  // the calibration if is after the showMenu() if, so it should exit menu to do
  // calibration
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
    if (grinderRuntimeLocksScaleSampling()) {
      Serial.println("Calibration samples locked by grinder");
      return;
    }
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
          // u8g2.setFont(FONT_M);
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
          // drawBleBox();
        } while (u8g2.nextPage());
      }
      if (input == 1) {
        calibrationEnsureSampleWindow();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          // 2行
          // FONT_M = u8g2_font_fub14_tn;
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
        // exit was selected, exit the calibration.
        calibrationFinish(true);
        return;
      }
      if (b_is_charging) {
        calibrationShowUsbWarning();
      }
      // scale.update();
      if (input == 0) {
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

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          // 2行
          // FONT_M = u8g2_font_fub14_tn;
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

void wifiUpdate() {
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"WiFi Update"), AM(), (char *)"WiFi Update");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  wifiOta();
  b_menu = false;
}

void showAbout() {
  actionMessage = FIRMWARE_VER;
  actionMessage2 = LINE3;
  b_showAbout = true;
  u8g2.setFont(FONT_S);
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
}

void showLogo() {
  b_showLogo = true;
  u8g2.firstPage();

  do {
    if (!b_showNumber) {
      // show logo
      u8g2.setFont(u8g2_font_logisoso22_tf);
      u8g2.drawStr(AC("Half"), 26, "Half");
      u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
      u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
    } else {
      // show number
      u8g2.drawXBM(121, 52, 7, 12, image_battery_4);  // 75-100% battery
      u8g2.drawXBM(3, 51, 5, 13, image_ble_enabled);
      u8g2.setFont(FONT_TIMER);
      u8g2.drawStr(AC("345"), LCDHeight - 8, "345");

      // Extract the integer part
      float number = 1234.5;
      int i_weightInt = (int)number;
      if (number >= 0) {
        b_negativeWeight = false;
      } else {
        b_negativeWeight = true;
      }

      // Calculate the decimal part by subtracting the integer part from the
      // number and then multiplying by 10 to shift the first decimal digit into
      // the integer place
      float decimalPart = number - (float)(i_weightInt);
      int i_weightFirstDecimal = abs((int)(decimalPart * 10));
      char integerStr[10] =
        "-0";  // to save the - sign if the input is between -1 to 0
      char decimalStr[10] = "0";
      if (number >= 0 || number <= -1) {
        snprintf(integerStr, sizeof(integerStr), "%d", i_weightInt);
      }
      snprintf(decimalStr, sizeof(decimalStr), "%d", i_weightFirstDecimal);
      u8g2.setFont(FONT_GRAM);
      int gramWidth = u8g2.getUTF8Width("g");  // Width of the "g" character
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
                   trim(integerStr));  // Assuming vertical position at 28,
                                       // adjust as needed
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
      // next time to show number
      b_showNumber = !b_showNumber;
      // exit
      b_showLogo = false;
    }
  }
}

void enableDebug() {
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"Exit Menu"), AM(), (char *)"Exit Menu");
  } while (u8g2.nextPage());
#ifdef BUZZER
  buzzer.off();
#endif
  delay(1000);
  b_debug = true;
  b_menu = false;
  // Optionally reset or perform an exit action
}

void calibrateVoltage() {
  actionMessage = "Calibrate 4.2v";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  const int numReadings = 50;  // Number of readings to average
  long adcSum = 0;

  // Take multiple ADC readings and calculate their sum
  for (int i = 0; i < numReadings; i++) {
    adcSum += analogRead(BATTERY_PIN);
    delay(10);  // Optional: Add a small delay between readings for stability
  }

  // Calculate the average ADC value
  float adcValue = adcSum / (float)numReadings;

  // Calculate the voltage at the pin and the actual battery voltage
  float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;
  float batteryVoltage = voltageAtPin * dividerRatio;

  // Calculate the calibration factor
  f_batteryCalibrationFactor = 4.2 / batteryVoltage;

  storagePutFloat(KEY_BAT_CAL, f_batteryCalibrationFactor);

  // Output the new calibration factor to the Serial Monitor
  Serial.print("Battery Voltage Factor set to: ");
  Serial.println(f_batteryCalibrationFactor);
}

// Navigate menu
void navigateMenu(int direction) {
  currentIndex = (currentIndex + direction + currentMenuSize) % currentMenuSize;
  currentSelection = currentMenu[currentIndex];
  Serial.print("currentIndex ");
  Serial.println(currentIndex);
  // showMenu();
}

// Select menu
// 5/5 count submenu items
void selectMenu() {
  // use the static way to avoid get size of dynamic array.
  if (currentSelection->subMenu) {
// Enter the submenu
#ifdef BUZZER
    if (currentSelection == &menuBuzzer) {
      currentMenu = buzzerMenu;
      currentMenuSize = getMenuSize(buzzerMenu);
    } else
#endif
      if (currentSelection == &menuCalibration) {
      currentMenu = calibrationMenu;
      currentMenuSize = getMenuSize(calibrationMenu);
    } else if (currentSelection == &menuWifi) {
      currentMenu = wifiUpdateMenu;
      currentMenuSize = getMenuSize(wifiUpdateMenu);
    } else if (currentSelection == &menuHeartbeat) {
      currentMenu = heartbeatMenu;
      currentMenuSize = getMenuSize(heartbeatMenu);
    } else if (currentSelection == &menuFlipScreen) {
      currentMenu = flipScreenMenu;
      currentMenuSize = getMenuSize(flipScreenMenu);
    } else if (currentSelection == &menuTimeOnTop) {
      currentMenu = timeOnTopMenu;
      currentMenuSize = getMenuSize(timeOnTopMenu);
    } else if (currentSelection == &menuBtnFuncWhileConnected) {
      currentMenu = btnFuncWhileConnectedMenu;
      currentMenuSize = getMenuSize(btnFuncWhileConnectedMenu);
    } else if (currentSelection == &menuAutoSleep) {
      currentMenu = autoSleepMenu;
      currentMenuSize = getMenuSize(autoSleepMenu);
    } else if (currentSelection == &menuQuickBoot) {
      currentMenu = quickBootMenu;
      currentMenuSize = getMenuSize(quickBootMenu);
    } else if (currentSelection == &menuDriftComp) {
      currentMenu = driftCompMenu;
      currentMenuSize = getMenuSize(driftCompMenu);
    } else if (currentSelection == &menuGrinder) {
      b_grinderMenuDirectEntry = false;
      currentMenu = grinderMenu;
      currentMenuSize = getMenuSize(grinderMenu);
    }
    // else if (currentSelection == &menuFactory) {
    //   currentMenu = factoryMenu;
    //   currentMenuSize = getMenuSize(factoryMenu);
    // }
    currentIndex = 0;
    currentSelection = currentMenu[currentIndex];
    // showMenu();
  } else if (currentSelection->action) {
    // Execute the action if available
    currentSelection->action();
  } else if (currentSelection->parentMenu) {
    if (currentSelection->parentMenu == &menuGrinder && b_grinderMenuDirectEntry) {
      exitMenu();
      return;
    }
    // Go back to the parent menu
    currentMenu = mainMenu;
    currentMenuSize = getMenuSize(mainMenu);
    currentIndex = 0;  // Reset to the first item in the parent menu
    currentSelection = currentMenu[currentIndex];
    // showMenu();
  }
}

// Display menu
void showMenu() {
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);

  u8g2.setFont(FONT_S);
  u8g2.firstPage();
  do {
    if (millis() - t_actionMessage < t_actionMessageDelay) {
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
      // currentMenuSize = getMenuSize(currentMenu);
      totalPages = (currentMenuSize + linesPerPage - 1) / linesPerPage;  // Calculate total pages
      char pageInfo[10];
      snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
      if (totalPages > 1)
        u8g2.drawStr(AR(pageInfo), u8g2.getMaxCharHeight(),
                     pageInfo);  // Show on top-right of the screen if more than
                                 // one page is needed.
      for (int i = 0; i < currentMenuSize; i++) {
        if (currentMenu[i] == currentSelection) {
          u8g2.drawStr(0, u8g2.getMaxCharHeight() * (i % linesPerPage + 1),
                       ">");  // Highlight current selection
        }
        if (i >= (currentPage - 1) * linesPerPage && i < currentPage * linesPerPage)
          u8g2.drawStr(10, u8g2.getMaxCharHeight() * (i % linesPerPage + 1),
                       currentMenu[i]->name);
      }
    }
  } while (u8g2.nextPage());
}

#endif
