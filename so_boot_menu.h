#ifndef BOOT_MENU_H
#define BOOT_MENU_H

#include "so_config.h"
#include "so_declare.h"
#include "so_display.h"

void setContainerWeight() {
  float containerWeight = 0;
  scale.setSamplesInUse(4);
  while (b_f_set_container) {
    buttonTare.check();
    buttonSet.check();
#ifdef FIVE_BUTTON
    buttonPower.check();
#endif  //FIVE_BUTTON
    switch (i_setContainerWeight) {
      case 0:
        if (scale.getTareStatus()) {
          //完成清零
          Serial.println("test1");
          buzzer.beep(2, 50);
        }
        Serial.println("test2");
        static boolean newDataReady = 0;
        static boolean scaleStable = 0;
        if (scale.update()) newDataReady = true;
        if (newDataReady) {
          f_weight_adc = scale.getData();
          circularBuffer[bufferIndex] = f_weight_adc;
          bufferIndex = (bufferIndex + 1) % windowLength;
          // calculate moving average
          f_weight_smooth = 0;
          for (int i = 0; i < windowLength; i++) {
            f_weight_smooth += circularBuffer[i];
          }
          f_weight_smooth /= windowLength;
          if (f_weight_smooth >= f_displayedValue - OledTolerance && f_weight_smooth <= f_displayedValue + OledTolerance) {
            // scale value is within tolerance range, do nothing
            // or weight is around 0, then set to 0.
            if (f_weight_smooth > -0.1 && f_weight_smooth < 0.1)
              f_displayedValue = 0.0;
          } else {
            // scale value is outside tolerance range, update displayed value
            f_displayedValue = f_weight_smooth;
            // print result to serial monitor
          }
          // print smoothed reading
          Serial.print(f_displayedValue);
          f_weight_container = f_displayedValue;
          char c_temp[10];
          dtostrf(f_weight_container, 7, i_decimal_precision, c_temp);
#ifdef CHINESE
          refreshOLED((char*)"设定容器重量", (char*)"按录入按钮保存", (char*)trim(c_temp));
#endif
#ifdef ENGLISH
          refreshOLED((char*)"Set container weight", (char*)"Press INPUT to save", (char*)trim(c_temp));
#endif
          Serial.println((char*)trim(c_temp));
        }
        break;
      case 1:
        delay(500);
        Serial.println("test3");
        // #if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
        //         EEPROM.begin(512);
        // #endif
        EEPROM.put(i_addr_container, f_weight_container);
        Serial.println("test3.5");
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
        EEPROM.commit();
        Serial.println("test4");
#endif
#ifdef CHINESE
        refreshOLED((char*)"装豆容器重量", (char*)"已保存");
#endif
#ifdef ENGLISH
        refreshOLED((char*)"Container weight", (char*)"Saved");
#endif
        Serial.println("test5");
        reset();
        break;
    }
  }
  Serial.print(F("ContainerWeight set to: "));
  Serial.print(f_weight_container);
}

// void setSample() {
//   static const char* sampleText[] = { "1", "2", "4", "8", "16", "32", "64", "128" };
//   while (b_f_set_sample) {
//     buttonPlus.check();   //左移动
//     buttonTare.check();   //右移动
//     buttonMinus.check();  //确认
//     buttonSet.check();    //二按钮确认
// #ifdef FIVE_BUTTON
//     buttonPower.check();
// #endif  //FIVE_BUTTON
//     if (i_sample < 0)
//       i_sample = 7;
//     if (i_sample > 7)
//       i_sample = 0;
//     switch (i_sample_step) {
//       case 0:  //左右移动
//         refreshOLED((char*)"设置采样数", (char*)sampleText[i_sample]);
//         break;
//       case 1:  //保存
//         Serial.println("保存");
// // #if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
// //         EEPROM.begin(512);
// // #endif
//         EEPROM.put(i_addr_sample, i_sample);
// #if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
//         EEPROM.commit();
// #endif
//         refreshOLED((char*)"采样数已保存", (char*)sampleText[i_sample]);
//         reset();
//         b_f_set_sample = false;
//         break;
//     }
//   }
//   Serial.print(F("sampleNumber: "));
//   Serial.print(i_sample);
//   Serial.print(F(" sample set to"));
//   Serial.println(sample[i_sample]);
// }

void cal() {
  char* c_calval = (char*)"";
  if (i_button_cal_status == 1) {
    scale.setSamplesInUse(16);
    Serial.println(F("***"));
    Serial.println(F("Start calibration:"));
    Serial.println(F("Place the load cell an a level stable surface."));
    Serial.println(F("Remove any load applied to the load cell."));
    Serial.println(F("Press Button to set the tare offset."));
#ifdef CHINESE
    refreshOLED((char*)"放置平稳后", (char*)"按清零按钮");
#endif
#ifdef ENGLISH
    refreshOLED((char*)"Let it settle, then", (char*)"Press TARE button");
#endif
    delay(1000);
#ifdef POWER_DOG
    digitalWrite(BUTTON_KEY, LOW);
    delay(100);
    digitalWrite(BUTTON_KEY, HIGH);
#endif

    boolean _resume = false;
    while (_resume == false) {

      scale.update();


      buttonSet.check();
      buttonTare.check();
#ifdef FIVE_BUTTON
      buttonPower.check();
#endif  //FIVE_BUTTON
      if (i_button_cal_status == 2) {
        Serial.println(F("Hands off Taring...3"));
#ifdef CHINESE
        refreshOLED((char*)"请勿触碰", (char*)"即将开始清零...3");

        delay(1000);
        refreshOLED((char*)"请勿触碰", (char*)"即将开始清零...2");

        delay(1000);
        refreshOLED((char*)"请勿触碰", (char*)"即将开始清零...1");

        delay(1000);
        refreshOLED((char*)"请勿触碰", (char*)"正在清零");
        scale.tare();
        Serial.println(F("Tare done"));
        refreshOLED((char*)"清零完成", (char*)"");
#endif
#ifdef ENGLISH
        refreshOLED((char*)"Hands off", (char*)"Tare in...3");

        delay(1000);
        refreshOLED((char*)"Hands off", (char*)"Tare in...2");

        delay(1000);
        refreshOLED((char*)"Hands off", (char*)"Tare in...1");

        delay(1000);
        refreshOLED((char*)"Hands off", (char*)"Taring");
        scale.tare();
        Serial.println(F("Tare done"));
        refreshOLED((char*)"Tare done", (char*)"");
#endif
        buzzer.beep(2, 50);

        delay(1000);
        _resume = true;
      }
    }
    Serial.println(F("Now, place your known mass on the loadcell."));
    Serial.println(F("Then send the weight of this mass (i.e. 100.0) from serial monitor."));
    //refreshOLED((char*)"放置100g砝码", (char*)"随后按清零按钮");
    //delay(1000);
#ifdef POWER_DOG
    digitalWrite(BUTTON_KEY, LOW);
    delay(100);
    digitalWrite(BUTTON_KEY, HIGH);
#endif
    float known_mass = 0;
    _resume = false;
    while (_resume == false) {
      scale.update();
      buttonSet.check();
      buttonTare.check();
#ifdef FIVE_BUTTON
      buttonPower.check();
#endif  //FIVE_BUTTON
      if (i_button_cal_status == 3) {
        known_mass = 100.0;
        if (known_mass != 0) {
          Serial.print(F("Known mass is: "));
          Serial.println(known_mass);
#ifdef CHINESE
          refreshOLED((char*)"放置100g砝码", (char*)"即将开始校准...3");
          delay(1000);
          refreshOLED((char*)"放置100g砝码", (char*)"即将开始校准...2");
          delay(1000);
          refreshOLED((char*)"放置100g砝码", (char*)"即将开始校准...1");
          delay(1000);
          refreshOLED((char*)"请勿触碰", (char*)"正在校准");
#endif
#ifdef ENGLISH
          refreshOLED((char*)"Put on 100g weight", (char*)"Re-calibration in...3");
          delay(1000);
          refreshOLED((char*)"Put on 100g weight", (char*)"Re-calibration in...2");
          delay(1000);
          refreshOLED((char*)"Put on 100g weight", (char*)"Re-calibration in...1");
          delay(1000);
          refreshOLED((char*)"Hands off", (char*)"Re-calibrating");
#endif
          _resume = true;
        }
      }
    }
    scale.refreshDataSet();                                     //refresh the dataset to be sure that the known mass is measured correct
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
#ifdef CHINESE
    refreshOLED((char*)"校准完成", (char*)trim(c_calval));
#endif
#ifdef ENGLISH
    refreshOLED((char*)"Re-calibration done", (char*)trim(c_calval));
#endif
    buzzer.beep(2, 50);
    delay(1000);
  }
  b_f_calibration = false;
}

void showInfo() {
  char* scaleInfo[] = { VERSION };
  refreshOLED(scaleInfo[0], scaleInfo[1], scaleInfo[2]);
  while (b_f_show_info) {
    buttonSet.check();
#ifndef TWO_BUTTON
    buttonPlus.check();
    buttonMinus.check();
#endif  //TWO_BUTTON
    buttonSet.check();
#ifdef FIVE_BUTTON
    buttonPower.check();
#endif  //FIVE_BUTTON
  }
}

#endif