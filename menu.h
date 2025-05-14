#ifndef MENU_H
#define MENU_H

const char *weights[] = { "Exit", "50g", "100g", "200g", "500g", "1000g" };
const float weight_values[] = { 0.0, 50.0, 100.0, 200.0, 500.0, 1000.0 };
bool b_showAbout = false;
bool b_showLogo = false;
bool b_showNumber = false;
String actionMessage = "Default";
String actionMessage2 = "Default";
long t_actionMessage = 0;
long t_actionMessageDelay = 1000;
template<typename T>
int getMenuSize(T &menu) {
  return sizeof(menu) / sizeof(menu[0]);
}

// Menu structure
struct Menu {
  const char *name;  //menu name
  void (*action)();  //what to do NULL for submenu
  Menu *subMenu;     //submenu NULL for none
  Menu *parentMenu;  //parentmenu NULL for root menu
};

// Function prototypes
void exitMenu();
void buzzerOn();
void buzzerOff();
void heartbeatOn();
void heartbeatOff();
void calibrate();
void drawButton();
void wifiUpdate();
void showAbout();
void showMenu();
void showLogo();
void calibrateVoltage();
void navigateMenu(int direction);
void selectMenu();
void enableDebug();

// Top-level menu options
// 1/5 define the 1st level menu
Menu menuExit = { "Exit", exitMenu, NULL, NULL };
Menu menuBuzzer = { "Buzzer", NULL, NULL, NULL };
Menu menuCalibration = { "Calibration", NULL, NULL, NULL };
Menu menuWiFiUpdate = { "WiFi Update", NULL, NULL, NULL };
Menu menuAbout = { "About", showAbout, NULL, NULL };
Menu menuLogo = { "Show Logo", showLogo, NULL, NULL };
Menu menuFactory = { "Factory", NULL, NULL, NULL };
Menu menuHeartbeat = { "Heartbeat", NULL, NULL, NULL };

// Buzzer submenu
// 2/5 define the 2st level menu
Menu menuBuzzerBack = { "Back", NULL, NULL, &menuBuzzer };
Menu menuBuzzerOn = { "Buzzer On", buzzerOn, NULL, &menuBuzzer };
Menu menuBuzzerOff = { "Buzzer Off", buzzerOff, NULL, &menuBuzzer };
Menu *buzzerMenu[] = { &menuBuzzerBack, &menuBuzzerOn, &menuBuzzerOff };

// Calibration submenu
Menu menuCalibrationBack = { "Back", NULL, NULL, &menuCalibration };
Menu menuCalibrate = { "Calibrate", calibrate, NULL, &menuCalibration };
Menu *calibrationMenu[] = { &menuCalibrationBack, &menuCalibrate };

// WiFi Update submenu
Menu menuWiFiUpdateBack = { "Back", NULL, NULL, &menuWiFiUpdate };
Menu menuWiFiUpdateOption = { "WiFi Update", wifiUpdate, NULL, &menuWiFiUpdate };
Menu *wifiUpdateMenu[] = { &menuWiFiUpdateBack, &menuWiFiUpdateOption };

// Heartbeat detection
Menu menuHeartbeatBack = { "Back", NULL, NULL, &menuHeartbeat };
Menu menuHeartbeatOn = { "Heartbeat On", heartbeatOn, NULL, &menuHeartbeat };
Menu menuHeartbeatOff = { "Heartbeat Off", heartbeatOff, NULL, &menuHeartbeat };
Menu *heartbeatMenu[] = { &menuHeartbeatBack, &menuHeartbeatOn, &menuHeartbeatOff };

// Menu menuFactoryBack = { "Back", NULL, NULL, &menuFactory };
// Menu menuCalibrateVoltage = { "Calibrate 4.2v", calibrateVoltage, NULL, &menuFactory };
// Menu menuFactoryDebug = { "Debug Info", enableDebug, NULL, &menuFactory };
// Menu *factoryMenu[] = { &menuFactoryBack, &menuCalibrateVoltage, &menuFactoryDebug };

// Main menu
// 3/5 write all the 1st menu to mainMenu
Menu *mainMenu[] = {
  &menuExit, &menuBuzzer, &menuCalibration, &menuWiFiUpdate, &menuAbout, &menuLogo, &menuHeartbeat
  //, &menuFactory
};
//  &menuHolder1, &menuHolder2, &menuHolder3, &menuHolder4,
//  &menuHolder5, &menuHolder6};
Menu **currentMenu = mainMenu;
Menu *currentSelection = mainMenu[0];
int currentMenuSize = getMenuSize(mainMenu);  // Top-level menu size
int currentIndex = 0;
const int linesPerPage = 4;                           // Maximum number of lines that can fit on the display
int currentPage = 0;                                  // Determine the current page
int totalPages = currentMenuSize / linesPerPage + 1;  // Calculate total pages

// 4/5 link all the submenus
void linkSubmenus() {
  // Link submenus
  menuBuzzer.subMenu = buzzerMenu[0];
  menuCalibration.subMenu = calibrationMenu[0];
  menuWiFiUpdate.subMenu = wifiUpdateMenu[0];
  menuHeartbeat.subMenu = heartbeatMenu[0];
  //menuFactory.subMenu = factoryMenu[0];
}

// Menu actions
void exitMenu() {
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"Exit Menu"), AM(), (char *)"Exit Menu");
  } while (u8g2.nextPage());
  buzzer.off();
  delay(1000);
  b_menu = false;
  // Optionally reset or perform an exit action
}

void buzzerOn() {
  if (b_beep == false) {
    b_beep = true;
    buzzer.beep(1, BUZZER_DURATION);
  }
  actionMessage = "Buzzer on";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  EEPROM.put(i_addr_beep, b_beep);
  EEPROM.commit();
  Serial.println("Buzzer On stored in EEPROM.");
}

void buzzerOff() {
  b_beep = false;
  actionMessage = "Buzzer off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  EEPROM.put(i_addr_beep, b_beep);
  EEPROM.commit();
  Serial.println("Buzzer off stored in EEPROM.");
}

void heartbeatOn() {
  b_requireHeartBeat = true;
  actionMessage = "Heartbeat On";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  EEPROM.put(i_addr_heartbeat, b_requireHeartBeat);
  EEPROM.commit();
  Serial.println("Heartbeat detection...On");
}

void heartbeatOff() {
  b_requireHeartBeat = false;
  actionMessage = "Heartbeat Off";
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  EEPROM.put(i_addr_heartbeat, b_requireHeartBeat);
  EEPROM.commit();
  Serial.println("Heartbeat detection...Off");
}

void calibrate() {
  b_menu = false;
  b_calibration = true;  //让按钮进入校准状态3
  i_calibration = 0;
  //calibration(0);
  //the calibration if is after the showMenu() if, so it should exit menu to do calibration
}

void calibration(int input) {
  if (b_calibration == true) {
    bool newDataReady = false;
    char *c_calval = (char *)"";
    if (i_button_cal_status == 1) {
      if (input == 0) {
        scale.setSamplesInUse(16);
        u8g2.firstPage();
        do {
#ifdef ROTATION_180
          u8g2.setDisplayRotation(U8G2_R2);
#else
          u8g2.setDisplayRotation(U8G2_R0);
#endif
          u8g2.setFontMode(1);
          u8g2.setDrawColor(1);
          u8g2.setFont(FONT_S);
          if (b_is_charging) {
            u8g2.drawUTF8(AC((char *)"Unplug scale"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Unplug scale");
            u8g2.drawUTF8(AC((char *)"to start calibration"), LCDHeight - i_margin_bottom, (char *)"to start calibration");
          } else {
            int x, y;
            x = 0;
            y = u8g2.getMaxCharHeight();
            u8g2.drawUTF8(x, y, "Calibration Weight");
            //u8g2.setFont(FONT_M);
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
          }

          u8g2.setDrawColor(2);
          drawButton();
          //drawBleBox();
        } while (u8g2.nextPage());
      }
      if (input == 1) {
        scale.setSamplesInUse(16);
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          //2行
          //FONT_M = u8g2_font_fub14_tn;
          u8g2.drawUTF8(AC((char *)"Remove all weight"), u8g2.getMaxCharHeight() + i_margin_top + 3, (char *)"Remove all weight");
          u8g2.drawUTF8(AC((char *)"to start calibration"), LCDHeight - i_margin_bottom - 3, (char *)"to start calibration");
        } while (u8g2.nextPage());
      }
    }
    if (i_button_cal_status == 2) {
      Serial.println("Before if check, i_cal_weight = " + String(i_cal_weight));

      if (i_cal_weight == 0 || b_is_charging) {
        //exit was selected, exit the calibration.
        i_button_cal_status = 0;
        b_calibration = false;
        b_menu = true;
        return;
      }
      //scale.update();
      if (input == 0) {
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Remove weight"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Remove weight");
          u8g2.drawUTF8(AC((char *)"from scale"), LCDHeight - i_margin_bottom, (char *)"from scale");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(2000);
      }
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom, (char *)"Wait: 3s");
      } while (u8g2.nextPage());

      buzzer.off();
      delay(1000);
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom, (char *)"Wait: 2s");
      } while (u8g2.nextPage());

      buzzer.off();
      delay(1000);

      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Calibrating 0g");
        u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom, (char *)"Wait: 1s");
      } while (u8g2.nextPage());

      buzzer.off();
      delay(1000);

      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"Calibrating 0g"), AM(), (char *)"Calibrating 0g");
      } while (u8g2.nextPage());

      scale.tare();
      Serial.println(F("0g calibration done"));
      u8g2.firstPage();
      u8g2.setFont(FONT_S);
      do {
        u8g2.drawUTF8(AC((char *)"0g calibration done"), AM(), (char *)"0g calibration done");
      } while (u8g2.nextPage());

      buzzer.beep(1, BUZZER_DURATION);

      buzzer.off();
      delay(1000);
      i_button_cal_status++;
    }
    if (i_button_cal_status == 3) {
      if (input == 0) {
        float known_mass = 0;
        scale.update();
        known_mass = weight_values[i_cal_weight];
        char buffer[50];
        snprintf(buffer, sizeof(buffer), "Place %s weight", weights[i_cal_weight]);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom, (char *)"Wait: 3s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom, (char *)"Wait: 2s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom, (char *)"Wait: 1s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {

          u8g2.drawUTF8(AC((char *)"Calibrating"), AM(), (char *)"Calibrating");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);
        double d_weight;
        for (int i = 0; i < DATA_SET; i++) {
          if (scale.update()) newDataReady = true;
          if (newDataReady) {
            d_weight = scale.getData();
            Serial.println(d_weight);
            newDataReady = false;
            delay(100);
          }
        }
        Serial.print("weight is ");
        Serial.println(d_weight);
        if (abs(d_weight) < 5) {
          u8g2.firstPage();
          u8g2.setFont(FONT_S);
          do {
            //2行
            //FONT_M = u8g2_font_fub14_tn;
            u8g2.drawUTF8(AC((char *)"No weight detected"), AM(), (char *)"No weight detected");
          } while (u8g2.nextPage());
          buzzer.off();
          delay(1000);
          //reject the weight and exit
          i_button_cal_status = 0;
          b_calibration = false;
          b_menu = true;
          return;
        }
        scale.refreshDataSet();  //refresh the dataset to be sure that the known mass is measured correct

        f_calibration_value = scale.getNewCalibration(known_mass);  //get the new calibration value
        Serial.print(F("New calibration value f: "));
        Serial.println(f_calibration_value);
        // #if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
        //     EEPROM.begin(512);
        // #endif
        EEPROM.put(i_addr_calibration_value, f_calibration_value);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
        EEPROM.commit();
#endif
        dtostrf(f_calibration_value, 10, 2, c_calval);
        Serial.print(F("New calibration value c: "));
        Serial.println(trim(c_calval));

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          //2行
          //FONT_M = u8g2_font_fub14_tn;
          u8g2.drawUTF8(AC((char *)"Recalibration done"), AM(), (char *)"Recalibration done");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        buzzer.beep(1, BUZZER_DURATION);
        buzzer.off();
        delay(1000);
        b_calibration = false;
      }
      if (input == 1) {
        scale.update();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Place any weight"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Place any weight");
          u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom, (char *)"Wait: 3s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Place any weight"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Place any weight");
          u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom, (char *)"Wait: 2s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Place any weight"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Place any weight");
          u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom, (char *)"Wait: 1s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);
        scale.update();
        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Reading weight"), AM(), (char *)"Reading weight");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);
        i_button_cal_status++;
      }
    }
    if (i_button_cal_status == 4) {
      float known_mass = 0;
      if (scale.update()) newDataReady = true;
      if (newDataReady) {
        float current_weight = scale.getData();
        Serial.println(current_weight);
        bool valid_mass = false;
        for (int i = 1; i <= 40; i++) {
          //check from 50g to 2kg
          if (current_weight > i * 50 - 2 && current_weight < i * 50 + 2) {
            //check if the weight is within 2g
            known_mass = i * 50;
            valid_mass = true;
            break;
          }
        }

        if (!valid_mass) {
          char buffer[50];
          snprintf(buffer, sizeof(buffer), "%f.0g weight detected", current_weight);
          Serial.println(F("Error: Invalid weight detected"));
          u8g2.firstPage();
          u8g2.setFont(FONT_S);
          do {
            u8g2.drawUTF8(AC((char *)"Error: Invalid"), u8g2.getMaxCharHeight() + i_margin_top, (char *)"Error: Invalid");
            u8g2.drawUTF8(AC((char *)trim(buffer)), LCDHeight - i_margin_bottom, (char *)trim(buffer));
          } while (u8g2.nextPage());
          buzzer.off();
          delay(1000);
          b_calibration = false;
          return;  // exit calibration
        }

        char buffer[50];
        snprintf(buffer, sizeof(buffer), "Place %.0fg weight", known_mass);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 3s"), LCDHeight - i_margin_bottom, (char *)"Wait: 3s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 2s"), LCDHeight - i_margin_bottom, (char *)"Wait: 2s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)trim(buffer)), u8g2.getMaxCharHeight() + i_margin_top, (char *)trim(buffer));
          u8g2.drawUTF8(AC((char *)"Wait: 1s"), LCDHeight - i_margin_bottom, (char *)"Wait: 1s");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Calibrating"), AM(), (char *)"Calibrating");
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        scale.setSamplesInUse(16);
        scale.refreshDataSet();                                     //refresh the dataset to be sure that the known mass is measured correct
        f_calibration_value = scale.getNewCalibration(known_mass);  //get the new calibration value
        Serial.print(F("New calibration value f: "));
        Serial.println(f_calibration_value);
        EEPROM.put(i_addr_calibration_value, f_calibration_value);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
        EEPROM.commit();
#endif
        dtostrf(f_calibration_value, 10, 2, c_calval);
        Serial.print(F("New calibration value c: "));
        Serial.println(trim(c_calval));

        u8g2.firstPage();
        u8g2.setFont(FONT_S);
        do {
          u8g2.drawUTF8(AC((char *)"Recalibration done"), AM(), (char *)"Recalibration done");
          //u8g2.drawUTF8(AC((char *)trim(c_calval)), LCDHeight - i_margin_bottom, (char *)trim(c_calval));
        } while (u8g2.nextPage());
        buzzer.off();
        delay(1000);

        buzzer.beep(1, BUZZER_DURATION);
        buzzer.off();
        delay(1000);
        b_calibration = false;
      }
    }
    scale.setSamplesInUse(1);
  }
}

void wifiUpdate() {
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    u8g2.drawStr(AC((char *)"WiFi Update"), AM(), (char *)"WiFi Update");
  } while (u8g2.nextPage());
  buzzer.off();
  delay(1000);
  wifiOta();
  b_menu = false;
}

void showAbout() {
  actionMessage = FIRMWARE_VER;
  actionMessage2 = PCB_VER;
  b_showAbout = true;
  u8g2.setFont(FONT_M);
  u8g2.firstPage();
  do {
    if (AC(actionMessage.c_str()) < 0)
      u8g2.setFont(FONT_S);
    u8g2.drawStr(AC(actionMessage.c_str()), AM() - 12, actionMessage.c_str());
    u8g2.drawStr(AC(actionMessage2.c_str()), AM() + 12, actionMessage2.c_str());
  } while (u8g2.nextPage());
  buzzer.off();
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
      //show logo
      u8g2.setFont(u8g2_font_logisoso22_tf);
      u8g2.drawStr(AC("Half"), 26, "Half");
      u8g2.drawBox(4, LCDHeight / 2, LCDWidth - 4 * 2, 2);
      u8g2.drawStr(AC("Decent"), LCDHeight - 2, "Decent");
    } else {
      //show number
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

      // Calculate the decimal part by subtracting the integer part from the number
      // and then multiplying by 10 to shift the first decimal digit into the integer place
      float decimalPart = number - (float)(i_weightInt);
      int i_weightFirstDecimal = abs((int)(decimalPart * 10));
      char integerStr[10] = "-0";  // to save the - sign if the input is between -1 to 0
      char decimalStr[10] = "0";
      if (number >= 0 || number <= -1) {
        dtostrf(i_weightInt, 7, 0, integerStr);  // Integer part, no decimal
      }
      dtostrf(i_weightFirstDecimal, 7, 0, decimalStr);
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
      u8g2.drawStr(x_integer, y, trim(integerStr));  // Assuming vertical position at 28, adjust as needed
      u8g2.drawStr(x_decimal, y, trim(decimalStr));
      if (number > -1000.0) {
        u8g2.setFont(FONT_GRAM);
        u8g2.drawStr(x_gram, y - 5, "g");
      }
    }
  } while (u8g2.nextPage());

  buzzer.off();
  delay(1000);
  while (b_showLogo) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      //next time to show number
      b_showNumber = !b_showNumber;
      //exit
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
  buzzer.off();
  delay(1000);
  b_debug = true;
  b_menu = false;
  // Optionally reset or perform an exit action
}

// void calibrateVoltage() {
//   actionMessage = "Calibrate 4.2v";
//   t_actionMessage = millis();
//   int adcValue = analogRead(BATTERY_PIN);                                   // Read the ADC value from the battery pin
//   float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;       // Calculate the voltage at the ADC pin
//   float batteryVoltage = voltageAtPin * dividerRatio;                       // Calculate the actual battery voltage using the voltage divider ratio
//   f_batteryCalibrationFactor = 4.2 / batteryVoltage;                        // Calculate the calibration factor from user input
//   EEPROM.put(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);  // Store the calibration factor in EEPROM

// #if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
//   EEPROM.commit();  // Commit changes to EEPROM to save the calibration factor
// #endif
//   Serial.print("Battery Voltage Factor set to: ");  // Output the new calibration factor to the Serial Monitor
//   Serial.println(f_batteryCalibrationFactor);
// }

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

  // Store the calibration factor in EEPROM
  EEPROM.put(i_addr_batteryCalibrationFactor, f_batteryCalibrationFactor);

#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
  EEPROM.commit();  // Commit changes to EEPROM
#endif

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
  //showMenu();
}

// Select menu
// 5/5 count submenu items
void selectMenu() {
  //use the static way to avoid get size of dynamic array.
  if (currentSelection->subMenu) {
    // Enter the submenu
    if (currentSelection == &menuBuzzer) {
      currentMenu = buzzerMenu;
      currentMenuSize = getMenuSize(buzzerMenu);
    } else if (currentSelection == &menuCalibration) {
      currentMenu = calibrationMenu;
      currentMenuSize = getMenuSize(calibrationMenu);
    } else if (currentSelection == &menuWiFiUpdate) {
      currentMenu = wifiUpdateMenu;
      currentMenuSize = getMenuSize(wifiUpdateMenu);
    } else if (currentSelection == &menuHeartbeat) {
      currentMenu = heartbeatMenu;
      currentMenuSize = getMenuSize(heartbeatMenu);
    }
    // else if (currentSelection == &menuFactory) {
    //   currentMenu = factoryMenu;
    //   currentMenuSize = getMenuSize(factoryMenu);
    // }
    currentIndex = 0;
    currentSelection = currentMenu[currentIndex];
    //showMenu();
  } else if (currentSelection->action) {
    // Execute the action if available
    currentSelection->action();
  } else if (currentSelection->parentMenu) {
    // Go back to the parent menu
    currentMenu = mainMenu;
    currentMenuSize = getMenuSize(mainMenu);
    currentIndex = 0;  // Reset to the first item in the parent menu
    currentSelection = currentMenu[currentIndex];
    //showMenu();
  }
}

// Display menu
void showMenu() {
#ifdef ROTATION_180
  u8g2.setDisplayRotation(U8G2_R2);
#else
  u8g2.setDisplayRotation(U8G2_R0);
#endif

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
        u8g2.drawStr(AC(actionMessage.c_str()), AM() - 12, actionMessage.c_str());
        u8g2.drawStr(AC(actionMessage2.c_str()), AM() + 12, actionMessage2.c_str());
      }
    } else {
      actionMessage == "Default";
      currentPage = currentIndex / linesPerPage + 1;
      //currentMenuSize = getMenuSize(currentMenu);
      totalPages = (currentMenuSize + linesPerPage - 1) / linesPerPage;  //Calculate total pages
      char pageInfo[10];
      snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
      if (totalPages > 1)
        u8g2.drawStr(AR(pageInfo), u8g2.getMaxCharHeight(), pageInfo);  // Show on top-right of the screen if more than one page is needed.
      for (int i = 0; i < currentMenuSize; i++) {
        if (currentMenu[i] == currentSelection) {
          u8g2.drawStr(0, u8g2.getMaxCharHeight() * (i % linesPerPage + 1), ">");  // Highlight current selection
        }
        if (i >= (currentPage - 1) * linesPerPage && i < currentPage * linesPerPage)
          u8g2.drawStr(10, u8g2.getMaxCharHeight() * (i % linesPerPage + 1), currentMenu[i]->name);
      }
    }
  } while (u8g2.nextPage());
}


#endif