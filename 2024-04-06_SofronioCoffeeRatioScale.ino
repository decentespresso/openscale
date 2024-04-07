/*
  2021-07-25 新增开机时校准，双击咖啡录入关机
  2021-07-26 新增开机按住清零校准，取消双击录入关机
  2021-08-02 新增开机修改sample，显示版本信息，手柄录入功能
  2021-08-07 v1.1 新增手柄录入功能
  2021-08-15 v1.1 去掉手柄录入（因为双头手柄含水量不一定），修复进入意式模式时未恢复参数，新增电量检测
  2021-09-01 v1.2 重新加入手柄录入 修复sample不可更改bug
  2021-09-03 v1.3 修复切换到意式模式直接计时问题 修复录入可能产生负值问题
  2021-10-02 v1.4 修复切换到意式模式 下液计时不清零问题
  2021-10-10 v1.5 修复手柄录入产生的计时问题 新增显示旋转功能
  2022-02-03 v1.6 二按钮模式
  2022-03-04 v1.7 流速计
  2022-04-12 v1.8 优化电量显示为0时闪屏
  2022-08-01 v1.9 尝试支持3.3v芯片
  2022-08-06 v2.0 换用rp2040，支持无操作自动关机
  2022-09-11 v2.1 支持双模式，支持六轴传感器侧放关机
  2022-11-02 v2.2 使用ESP32 wemos lite，支持esp32 休眠，支持esp32电容按钮开关机，去掉5v充放一体单元，去掉了电量显示
  2023-02-11 v2.3 倾斜时不开机，避免误触
    bug fix:  setsamplesinuse只能缩小原来config.h中的SAMPLES，不能增加。
  2023-03-06 v2.4 大幅改进显示稳定性
  2023-03-11 v3.0 WiFi OTA，更换字体大小
  2023-06-24 v3.1 去掉WiFi功能，加入蓝牙串口，可自定义所有参数。
  2023-12-11 v3.2 ESPNow无线传输参数显示
  2023-12-23 v3.3 ESPNow左键开启，和蓝牙一样，加入ssd1312
  2024-01-27 v3.4 加入静音后代替蜂鸣器的LED
  2024-03-25 v3.5 Add early verion of English translation
  2024-04-06 v4.0 Add BLE and uuid.

  todo
  开机M进入菜单
  //2023-02-11 关闭蜂鸣器 DONE(2023-06-25)
  2023-03-06 使用enum菜单
  2024-03-23 Impliment ble function using service uuid and charactoristic uuid to let other apps/devices to get the scale data, or have it tared.
*/

//include

#include <Arduino.h>

#if defined(ESP8266) || defined(ESP32) || defined(AVR) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
#include <EEPROM.h>
#endif

//#include "so_config.h"
#include "so_parameter.h"
#include "so_power.h"
#include "so_gyro.h"
#include "so_display.h"
//#include "so_menu.h"
#include "so_boot_menu.h"
#include "so_declare.h"
#include "so_wifi_ota.h"
#include "so_espnow.h"
#include "so_config.h"

#include <BluetoothSerial.h>
BluetoothSerial SerialBT;

// #ifndef BT
// #ifdef DEBUG_BT
// #include <BluetoothSerial.h>
// BluetoothSerial SerialBT;
// #endif
// #endif

//functions 函数

//ble
// Function to calculate XOR for validation (assuming this might still be needed)
uint8_t calculateXOR(uint8_t *data, size_t len) {
  uint8_t xorValue = 0x03;                // Starting value for XOR as per your example
  for (size_t i = 1; i < len - 1; i++) {  // Start from 1 to len - 1 assuming last byte is XOR value
    xorValue ^= data[i];
  }
  return xorValue;
}

// Encode weight into two bytes, big endian
void encodeWeight(float weight, byte &byte1, byte &byte2) {
  int weightInt = (int)(weight * 10);  // Convert to grams * 10
  byte1 = (byte)((weightInt >> 8) & 0xFF);
  byte2 = (byte)(weightInt & 0xFF);
}

// This callback will be invoked when a device connects or disconnects
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  uint8_t calculateChecksum(uint8_t *data, size_t len) {
    uint8_t xorSum = 0;
    // Iterate over each byte in the data, excluding the last one assumed to be the checksum
    for (size_t i = 0; i < len - 1; i++) {
      xorSum ^= data[i];
    }
    return xorSum;
  }

  // Validate the checksum of the data
  bool validateChecksum(uint8_t *data, size_t len) {
    if (len < 2) {  // Need at least 1 byte of data and 1 byte of checksum
      return false;
    }
    uint8_t expectedChecksum = data[len - 1];
    uint8_t calculatedChecksum = calculateChecksum(data, len);
    return expectedChecksum == calculatedChecksum;
  }
  /*  
        Weight received on	FFF4 (0000FFF4-0000-1000-8000-00805F9B34FB)

        Firmware v1.0 and v1.1 sends weight as a 7 byte message:
        03CE 0000 0000 CD = 0.0 grams
        03CE 0065 0000 A8 = 10.1 grams
        03CE 0794 0000 5E = 194.0 grams
        03CE 1B93 0000 5E = 705.9 grams
        03CE 2BAC 0000 4A = 1118.0 grams

        Firmware v1.2 and newer sends weight with a timestamp as a 10 byte message:
        03CE 0000 010203 0000 CD = 0.0 grams - (1 minute, 2 seconds, 3 milliseconds)
        03CE 0065 010204 0000 A8 = 10.1 grams - (1 minute, 2 seconds, 4 milliseconds)
        03CE 0794 010205 0000 5E = 194.0 grams - (1 minute, 2 seconds, 5 milliseconds)
        03CE 1B93 010206 0000 5E = 705.9 grams - (1 minute, 2 seconds, 6 milliseconds)
        03CE 2BAC 010207 0000 4A = 1118.0 grams - (1 minute, 2 seconds, 7 milliseconds)

        030A LED and Power
        LED on [requires v1.1 firmware]
        030A 0101 000009 (grams)
        030A 0101 010008 (ounces) 
        LED off	
        030A 0000 000009
        Power off (new in v1.2 firmware)
        030A 0200 00000B

        030B Timer
        Timer start	
        030B 0300 00000B
        Timer stop	
        030B 0000 000008
        Timer zero	
        030B 0200 00000A

        030F Tare (set weight to zero)	
        030F 0000 00000C
        030F B900 0000B5

        
*/

  void onWrite(BLECharacteristic *pWriteCharacteristic) {
    Serial.print("Timer");
    Serial.print(millis());
    Serial.print(" onWrite counter:");
    Serial.println(i_onWrite_counter++);
    if (pWriteCharacteristic != nullptr) {                         // Check if the characteristic is valid
      size_t len = pWriteCharacteristic->getLength();              // Get the data length
      uint8_t *data = (uint8_t *)pWriteCharacteristic->getData();  // Get the data pointer

      // Optionally print the received HEX for verification or debugging
      Serial.print("Received HEX: ");
      for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) {  // Check if the byte is less than 0x10
          Serial.print("0");   // Print a leading zero
        }
        Serial.print(data[i], HEX);  // Print the byte in HEX
      }
      Serial.println();  // New line for readability
      if (data[0] == 0x03) {
        if (data[1] == 0x0F) {
          if (validateChecksum(data, len)) {
            Serial.println("Valid checksum for tare operation. Taring");
          } else {
            Serial.println("Invalid checksum for tare operation.");
          }
          scale.tare();
        } else if (data[1] == 0x0A) {
          if (data[2] == 0x00) {
            Serial.println("LED off detected.");
          } else if (data[2] == 0x01) {
            Serial.println("LED on detected.");
          } else if (data[2] == 0x02) {
            Serial.println("Power off detected.");
            shut_down_now_nobeep();
          }
        } else if (data[1] == 0x0B) {
          if (data[2] == 0x03) {
            Serial.println("Timer start detected.");
            stopWatch.start();
          } else if (data[2] == 0x00) {
            Serial.println("Timer stop detected.");
            stopWatch.stop();
          } else if (data[2] == 0x02) {
            Serial.println("Timer zero detected.");
            stopWatch.reset();
          }
        }
      }
    }
  }
};

//buttons

void aceButtonHandleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  power_off(-1);  //reset power off timer
  int pin = button->getPin();
  switch (eventType) {
    case AceButton::kEventPressed:
      buzzer.beep(1, 50);
    //   continue;
    case AceButton::kEventReleased:
    case AceButton::kEventClicked:
      //buzzer.beep(1, 100);
      switch (pin) {
        case BUTTON_SET:
          buttonSet_Clicked();
          break;
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
        case BUTTON_PLUS:
          buttonPlus_Clicked();
          break;
        case BUTTON_MINUS:
          buttonMinus_Clicked();
          break;
#endif
        case BUTTON_TARE:
          buttonTare_Clicked();
          break;
#ifdef FIVE_BUTTON
        case BUTTON_POWER:
          Serial.println("BUTTON_POWER Clicked");
          shut_down_now_nobeep();
          break;
#endif  //FIVE_BUTTON
      }
      break;
    case AceButton::kEventLongPressed:
      switch (pin) {
        case BUTTON_SET:
          if (b_f_extraction) {
            //意式模式下 长按回归普通模式
            //if in espresso mode, long press to go to pure weighing mode.
            buzzer.beep(1, 200);
            b_f_extraction = !b_f_extraction;
          } else {
            buzzer.beep(1, 200);
            b_f_minus_container = !b_f_minus_container;
          }
          break;
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
        case BUTTON_PLUS:
          //buzzer.beep(1, 100);
          break;
#endif
        case BUTTON_TARE:
          buzzer.beep(1, 200);
          if (b_f_calibration) {
            reset();
          } else {
            if (b_f_extraction) {
              i_display_rotation = !i_display_rotation;
            } else {
              if (b_f_mode != 0)
                b_f_mode = 0;
              else
                b_f_mode = 1;
              // Serial.print("b_f_mode: ");
              // Serial.println(b_f_mode);
              EEPROM.put(i_addr_mode, b_f_mode);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
              EEPROM.commit();
#endif
            }
          }
          break;
      }
      break;
    case AceButton::kEventRepeatPressed:
      switch (pin) {
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
        case BUTTON_PLUS:
          buttonPlus_Clicked();
          break;
        case BUTTON_MINUS:
          buttonMinus_Clicked();
          break;
#endif
      }
      break;

      // case AceButton::kEventDoubleClicked:
      //   switch (pin) {
      // #ifndef FIVE_BUTTON
      //         case BUTTON_SET:
      //           Serial.println("BUTTON_SET Double Clicked");
      //           shut_down_now_nobeep();
      //           break;
      // #endif  //FIVE_BUTTON
      // }
      break;
  }
}

void buttonSet_Clicked() {

  if (digitalRead(BUTTON_TARE) == LOW) {
    Serial.print("BUTTON_SET and TARE clicked together");
    Serial.println("Going to sleep now.");
    shut_down_now_nobeep();
  }

  if (millis() - t_button_pressed > 1000) {
    if (b_f_calibration) {
      reset();
    }
    // if (b_f_set_sample)
    //   i_sample++;
    if (b_f_set_container) {
      Serial.println("setContainerWeight()");
      i_setContainerWeight = 1;
    }
    if (b_f_show_info)
      b_f_show_info = false;
    if (b_f_extraction) {
      if (stopWatch.isRunning() == false) {
        //进入萃取模式1s后可以操作的时钟
        if (stopWatch.elapsed() == 0) {
          //初始态 时钟开始
          delay(500);
          stopWatch.start();
          Serial.println("时钟开始");  //timer start
        }
        // Serial.print(stopWatch.elapsed());
        // Serial.print(" ");
        // Serial.println("stopWatch.start();");
        if (stopWatch.elapsed() > 0) {
          //停止态 时钟清零
          stopWatch.reset();
          Serial.println("时钟复位");  //timer reset
        }
      } else {
        //在计时中 按一下则结束计时 停止冲煮 （固定参数回头再说）
        stopWatch.stop();
        Serial.println("停止计时");
      }

    } else {
      //普通模式录入粉重 切换到萃取模式
      //input ground weight to the scale, and switch to extration mode.
      bool newDataReady = false;
      if (scale.update())
        newDataReady = true;
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
        if (b_f_minus_container) {
          //去掉手柄重量模式
          f_weight_dose = f_displayedValue - f_weight_container;
          //b_f_minus_container = false;
          if (f_weight_dose < 3)
            f_weight_dose = f_weight_default_coffee;  //不足3g 录入为默认值（20g）配合手柄模式使用
                                                      //no greater than 3 grams, then the ground weight is set to preset value. inside so_prameter.h INPUTCOFFEEPOUROVER and INPUTCOFFEEESPRESSO
        } else {
          if (f_weight_before_input < 3)
            f_weight_dose = f_weight_default_coffee;  //不足3g 录入为默认值（20g）
          else
            f_weight_dose = f_displayedValue;
        }
        initExtration();
        b_f_ready_to_brew = false;
        stopWatch.stop();
        stopWatch.reset();
        if (b_f_mode)
          scale.tareNoDelay();
        b_f_extraction = true;
      }
    }
    t_button_pressed = millis();
  }
}

void buttonPlus_Clicked() {
  if (millis() - t_button_pressed > 1000) {
    // if (b_f_set_sample) {
    //   //设置采样数
    //set sample number.
    //   i_sample++;
    // }
    if (b_f_show_info) {
      //显示信息
      //show some product info
      b_f_show_info = false;
    }
    if (b_f_extraction) {
      //萃取模式
      //extration mode
      f_weight_dose = f_weight_dose + 0.1;
      //four button version, to plus 0.1g ground weight.
    } else {
      //纯称重模式
      //pure weighing mode
      b_f_minus_container_button = !b_f_minus_container_button;  //是否减去咖啡手柄重量
    }
    t_button_pressed = millis();
  }
}

void buttonMinus_Clicked() {
  if (millis() - t_button_pressed > 1000) {
    // if (b_f_set_sample) {
    //   i_sample--;
    // } else
    if (b_f_show_info) {
      b_f_show_info = false;
    } else if (b_f_extraction) {
      //萃取模式 按一下-0.1g
      //four button version, to minus 0.1g ground weight.
      if (f_weight_dose - 0.1 > 0)
        f_weight_dose = f_weight_dose - 0.1;
    } else {
      if (i_decimal_precision == 1)
        i_decimal_precision = 2;
      else
        i_decimal_precision = 1;
    }
    t_button_pressed = millis();
  }
}

void buttonTare_Clicked() {
  if (millis() - t_button_pressed > 1000) {
    // if (b_f_set_sample) {
    //   i_sample_step = 1;
    //   //setSample();  //保存sample设定
    //   return;
    // }
    if (b_f_show_info) {
      b_f_show_info = false;
      return;
    }
    if (b_f_calibration) {
      i_button_cal_status++;
      Serial.print("i_button_cal_status:");
      Serial.println(i_button_cal_status);
      return;
    }
    if (b_f_extraction) {
      if (stopWatch.isRunning())
        //在计时中 按一下则结束计时 停止冲煮
        //press button to stop the timer, and stop the brewing.
        stopWatch.stop();
      else {
        //萃取模式归零
        //不在计时中 重量归零 时间归零
        //a tare on extracion mode.
        //timer not runing, zero the scale and timer.
        b_f_weight_quick_zero = true;
        if (!b_f_extraction)
          f_weight_dose = 0;
        stopWatch.reset();
        t_extraction_begin = 0;
        t_extraction_first_drop = 0;
        t_extraction_first_drop_num = 0;
        t_extraction_last_drop = 0;
        //123scale.tareNoDelay();
        delay(500);
        scale.tare();
      }
    } else if (b_f_set_container) {
      delay(500);
      scale.tare();
    } else {
      //普通归零
      //a standard tare.
      //f_temp_tare = f_filtered_temperature;
      b_f_weight_quick_zero = true;
      delay(500);
      scale.tare();
    }
    t_button_pressed = millis();
  }
}

void initExtration() {
  stopWatch.reset();
  t_extraction_begin = 0;  //开始萃取打点
  //t_ for time stamp on:
  //extration begins
  t_extraction_first_drop = 0;  //下液第一滴打点
  //first drop
  t_extraction_first_drop_num = 0;
  t_extraction_last_drop = 0;  //下液结束打点
  //last drop
  tareCounter = 0;  //不稳计数器
  //if it's stable
  t_auto_tare = 0;  //自动归零打点
  //auto tare time stamp
  t_auto_stop = 0;  //下液停止打点
  //auto stop time stamp
  t_scale_stable = 0;  //稳定状态打点
  //scale stable time stamp
  t_time_out = 0;  //超时打点
  //time to long time stamp
  t_last_weight_adc = 0;  //最后一次重量输出打点
  //last adc reading time stamp
}

void button_init() {

  pinMode(BUTTON_SET, INPUT_PULLUP);
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  pinMode(BUTTON_PLUS, INPUT_PULLUP);
  pinMode(BUTTON_MINUS, INPUT_PULLUP);
#endif
  pinMode(BUTTON_TARE, INPUT_PULLUP);

#ifdef FIVE_BUTTON
  pinMode(BUTTON_POWER, INPUT_PULLUP);
  buttonPower.init(BUTTON_POWER);
#endif  //FIVE_BUTTON

  buttonSet.init(BUTTON_SET);
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  buttonPlus.init(BUTTON_PLUS);
  buttonMinus.init(BUTTON_MINUS);
#endif
  buttonTare.init(BUTTON_TARE);

  config1.setEventHandler(aceButtonHandleEvent);
  //config1.setFeature(ButtonConfig::kFeatureClick);
  config1.setFeature(ButtonConfig::kFeatureLongPress);
  config1.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  config1.setFeature(ButtonConfig::kFeatureRepeatPress);
  //config1.setFeature(ButtonConfig::kFeatureDoubleClick);
  //config1.setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
  config1.setFeature(ButtonConfig::kFeatureSuppressAfterClick);
  //config1.setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
  config1.setRepeatPressInterval(10);
  //config1.setDoubleClickDelay(DOUBLECLICK);
}


void setup() {
#ifdef ESP32
  Wire.begin(I2C_SDA, I2C_SCL);
#endif
  delay(50);  //有些单片机会重启两次
  //some soc may reset twice
  Serial.begin(115200);

  while (!Serial)
    ;
  delay(200);

  BLEDevice::init("Decent Scale");

  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE Service
  BLEService *pService = pServer->createService(SUUID_DECENTSCALE);


  pWriteCharacteristic = pService->createCharacteristic(
    CUUID_DECENTSCALE_WRITE,
    BLECharacteristic::PROPERTY_WRITE);
  pWriteCharacteristic->setCallbacks(new MyCallbacks());
  // Create BLE Characteristic for RX
  pReadCharacteristic = pService->createCharacteristic(
    CUUID_DECENTSCALE_READ,
    BLECharacteristic::PROPERTY_READ
      | BLECharacteristic::PROPERTY_NOTIFY
    //| BLECharacteristic::PROPERTY_WRITE
    //| BLECharacteristic::PROPERTY_INDICATE
    //| BLECharacteristic::PROPERTY_BROADCAST
    //| BLECharacteristic::PROPERTY_WRITE_NR
  );

  pReadCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting for a client connection to notify...");


#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040) || defined(ESP32C3)
  EEPROM.begin(512);
#endif
#ifdef DEBUG_BT
  SerialBT.begin("soso D.R.S");
#endif
  //delay(2000);
  Serial.println("Begin!");
  MPU6050_init();
#ifdef GYROFACEUP
  if (gyro_z() < 8) {
    Serial.print("gyro_z:");
    Serial.println(gyro_z());
    shut_down_now_accidentTouch();
  }
#endif
#ifdef GYROFACEDOWN
  if (gyro_z() > -8) {
    Serial.print("gyro_z:");
    Serial.println(gyro_z());
    shut_down_now_accidentTouch();
  }
#endif
#if defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
  analogReadResolution(ADC_BIT);
#endif
  button_init();


  pinMode(BUZZER, OUTPUT);
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  pinMode(BUTTON_KEY, INPUT_PULLUP);
  pinMode(BUTTON_DEBUG, INPUT_PULLUP);
#endif
#ifdef CHECKBATTERY
  pinMode(BATTERY_LEVEL, INPUT);
  pinMode(USB_LEVEL, INPUT);
#endif  //CHECKBATTERY

#ifndef ESP32
#if defined(HW_SPI) || defined(SW_SPI)
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_CS, LOW);
  digitalWrite(OLED_DC, LOW);
  digitalWrite(OLED_RST, HIGH);
#endif  //defined(HW_SPI) || defined(SW_SPI)
#endif  //ndef ESP32
  EEPROM.get(i_addr_beep, b_f_beep);
  buzzer.beep(1, 100);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(255);
  //char* welcome = "soso E.R.S"; //欢迎文字
  //refreshOLED(welcome);
  power_off(15);
#ifdef WELCOME
  EEPROM.get(i_addr_welcome, str_welcome);
  str_welcome.trim();
// Serial.print("str_welcome.length() = ");
// Serial.println(str_welcome.length());
// Serial.println(str_welcome);
#ifdef ROTATION_180
  u8g2.setDisplayRotation(U8G2_R2);
#else
  u8g2.setDisplayRotation(U8G2_R0);
#endif
  if (str_welcome.length() == 127)
    refreshOLED(WELCOME);
  else
    refreshOLED((char *)str_welcome.c_str(), FONT_EXTRACTION);
  delay(1000);
#endif  //WELCOME
  stopWatch.setResolution(StopWatch::SECONDS);
  stopWatch.start();
  stopWatch.reset();

  i_button_cal_status = 1;  //校准状态归1
  //calibration status goes to 1
  unsigned long stabilizingtime = 500;  //去皮时间(毫秒)，增加可以提高去皮精确度
  //taring duration. longer for better reading.
  boolean _tare = true;  //电子秤初始化去皮，如果不想去皮则设为false
  //whether the scale will tare on start.
  scale.begin();
  scale.start(stabilizingtime, _tare);

  EEPROM.get(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
  EEPROM.get(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
  // Serial.print("INPUTCOFFEEPOUROVER:");
  // Serial.print(INPUTCOFFEEPOUROVER);
  // Serial.print(" INPUTCOFFEEESPRESSO:");
  // Serial.println(INPUTCOFFEEESPRESSO);
  if (isnan(INPUTCOFFEEPOUROVER)) {
    INPUTCOFFEEPOUROVER = 16.0;
    EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }
  if (isnan(INPUTCOFFEEESPRESSO)) {
    INPUTCOFFEEESPRESSO = 18.0;
    EEPROM.put(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }
  if (b_f_beep != 0 && b_f_beep != 1) {
    b_f_beep = 1;
    EEPROM.put(i_addr_beep, b_f_beep);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }
  //读取模式
  EEPROM.get(i_addr_mode, b_f_mode);
  // Serial.print("b_f_mode: ");
  // Serial.println(b_f_mode);
  if (b_f_mode > 1) {
    b_f_mode = 0;
    EEPROM.put(i_addr_mode, b_f_mode);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }
  if (b_f_mode < 0) {
    b_f_mode = 0;
    EEPROM.put(i_addr_mode, b_f_mode);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }

  //检查手柄重量合法性
  //check eeprom container value is valid.
  EEPROM.get(i_addr_container, f_weight_container);
  if (isnan(f_weight_container)) {
    f_weight_container = 0;  //手柄重量为0
                             //保存0值
                             //if container is 0 grams, then save it to eeprom.
    EEPROM.put(i_addr_container, f_weight_container);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
    EEPROM.commit();
#endif
  }

  //检查校准值合法性
  EEPROM.get(i_addr_calibration_value, f_calibration_value);
  if (isnan(f_calibration_value)) {
    b_f_calibration = true;  //让按钮进入校准状态3
    //put calibration to status 3
    cal();  //无有效读取，进入校准模式
    //eeprom calibration value is not valid, go to calibration procedure.
  } else
    scale.setCalFactor(f_calibration_value);  //设定校准值

    //检查sample值合法性
    //check sample number is valid
    // EEPROM.get(i_addr_sample, i_sample);
    // if (isnan(i_sample)) {
    //   b_f_set_sample = true;
    //   i_sample = 0;  //读取失败 默认值为3 对应sample为8
    //   i_sample_step = 0;
    //   setSample();
    // }
#ifdef DEBUG
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_SET) == HIGH)
    b_f_debug = true;
  if (digitalRead(BUTTON_DEBUG) == LOW && digitalRead(BUTTON_SET) == LOW)
    b_f_debug_battery = true;
#endif  //DEBUG

//重新校准
//recalibration
#ifdef CAL
  if (digitalRead(BUTTON_SET) == LOW && digitalRead(BUTTON_TARE) == LOW) {
    b_f_calibration = true;  //让按钮进入校准状态3
    //go to calibration status 3
    cal();  //无有效读取，进入校准模式
            //calibration value is not valid, go to calibration procedure.
  }
#endif

//wifiota
#ifdef WIFI
  if (digitalRead(BUTTON_SET) == LOW) {
    wifiOTA();
  }
#endif

#if DEBUG
  Serial.print("digitalRead(BUTTON_SET):");
  Serial.print(digitalRead(BUTTON_SET));
  Serial.print("\tdigitalRead(BUTTON_SET):");
  Serial.println(digitalRead(BUTTON_SET));
#endif

  //灵敏度设置
  //sensitivity
  // if (digitalRead(BUTTON_SET) == LOW) {
  //   b_f_set_sample = true;
  //   i_sample_step = 0;
  //   setSample();
  // }

  //录入手柄/接粉杯重量
  //get the containter weight and save to eeprom
  if (digitalRead(BUTTON_TARE) == LOW) {
    b_f_set_container = true;
    setContainerWeight();
  }

  if (digitalRead(BUTTON_SET) == LOW) {
    SerialBT.begin("D.R.S.");
    setContainerWeight();

    WiFi.mode(WIFI_STA);
    // Print MAC address
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());

    // Disconnect from WiFi
    WiFi.disconnect();

    if (esp_now_init() == ESP_OK) {
      Serial.println("ESPNow Init Success");
      esp_now_register_recv_cb(receiveCallback);
      esp_now_register_send_cb(sentCallback);
      b_f_espnow = true;
    } else {
      Serial.println("ESPNow Init Failed");
      // Retry InitESPNow, add a counte and then restart?
      // InitESPNow();
      // or Simply Restart
      delay(3000);
      ESP.restart();
    }
  }

//4按键模式时显示信息
#ifndef TWO_BUTTON
  if (digitalRead(BUTTON_MINUS) == LOW) {
    b_f_show_info = true;
    showInfo();
    delay(2000);
  }
#endif
  Serial.print("Welcome: ");
  if (str_welcome.length() == 127)
    Serial.print(WELCOME1);
  else
    Serial.print(str_welcome);
  Serial.print("\t");
  Serial.print(WELCOME2);
  Serial.print("\t");
  Serial.println(WELCOME3);
  Serial.print("Info: ");
  Serial.print(LINE1);
  Serial.print("\t");
  Serial.print(LINE2);
  Serial.print("\t");
  Serial.print(LINE3);
  Serial.print("\tScale Type: ");
  if (b_f_mode)
    Serial.println("ESPRESSO");
  else
    Serial.println("POUROVER");

  Serial.print("Cal_Val: ");
  Serial.print(f_calibration_value);
  // Serial.print("\tSample[");
  // Serial.print(i_sample);
  // Serial.print("]: ");
  // Serial.print(sample[i_sample]);
  Serial.print(F("\tContainer: "));
  Serial.print(f_weight_container);
  Serial.print("g");
  Serial.print(F("\tDefaultPourOver: "));
  Serial.print(INPUTCOFFEEPOUROVER);
  Serial.print("g");
  Serial.print(F("\tDefaultEspresso: "));
  Serial.print(INPUTCOFFEEESPRESSO);
  Serial.println("g");

  Serial.println("Button:\tSET\tTare\tPower");
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  Serial.println("Button:\tSET\tPlus\tMinus\tTare\tPower");
#endif
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(BUTTON_SET);
  Serial.print("\t");
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  Serial.print(BUTTON_PLUS);
  Serial.print("\t");
  Serial.print(BUTTON_MINUS);
  Serial.print("\t");
#endif
  Serial.print(BUTTON_TARE);
  Serial.print("\t");
  Serial.println(GPIO_NUM_BUTTON_POWER);
  Serial.println("Button:\tI2C_SDA\tI2C_SCK\t711SDA\t711SCK\tBUZZER");
  Serial.print("Pin:");
  Serial.print("\t");
  Serial.print(I2C_SDA);
  Serial.print("\t");
  Serial.print(I2C_SCL);
  Serial.print("\t");
  Serial.print(HX711_SDA);
  Serial.print("\t");
  Serial.print(HX711_SCL);
  Serial.print("\t");
  Serial.println(BUZZER);

  scale.setCalFactor(f_calibration_value);  //设置偏移量
  //set the calibration value
  //scale.setSamplesInUse(sample[i_sample]);  //设置灵敏度
  scale.setSamplesInUse(1);  //设置灵敏度

#ifdef CHECKBATTERY
  f_up_battery = analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
  t_up_battery = millis();
#endif  //CHECKBATTERY
  scale.tareNoDelay();
  Serial.println("Setup complete...");
  // while (!b_f_ota)
  //   ;
}

void pourOverScale() {
  if (f_weight_dose < 3)
    f_weight_dose = INPUTCOFFEEPOUROVER;
  //checkBrew
  if (f_displayedValue > 1 && b_f_ready_to_brew == true && b_f_weight_quick_zero == false && (millis() - t_ready_to_brew > 3000)) {
    stopWatch.start();
    buzzer.beep(1, 100);
    b_f_ready_to_brew = false;
  }
  static bool newDataReady = 0;
  static bool scaleStable = 0;
  if (scale.update()) newDataReady = true;
  if (newDataReady) {
    f_weight_adc = scale.getData();
    newDataReady = 0;
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

    if (millis() > t_flow_rate + 1000) {
      f_flow_rate = f_displayedValue - f_flow_rate_last_weight;
      if (f_flow_rate < 0)
        f_flow_rate = 0;
      dtostrf(f_flow_rate, 7, 1, c_flow_rate);
      f_flow_rate_last_weight = f_displayedValue;
      t_flow_rate = millis();
    }

    dtostrf(f_displayedValue, 7, 1, c_weight);
    if (b_weight_in_serial == true)
      Serial.println(trim(c_weight));
  }

  //记录咖啡粉时，将重量固定为0
  //during the coffee ground input, set the reading to 0 to prevent from misreading
  if (scale.getTareStatus()) {
    //完成清零
    buzzer.beep(2, 50);
    b_f_ready_to_brew = true;
    t_ready_to_brew = millis();
    b_f_weight_quick_zero = false;
  }
  if (b_f_weight_quick_zero)
    f_displayedValue = 0.0;
  float ratio_temp = f_displayedValue / f_weight_dose;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (f_weight_dose < 0.1)
    ratio_temp = 0.0;
  dtostrf(ratio_temp, 7, i_decimal_precision, c_brew_ratio);
  dtostrf(f_displayedValue, 7, i_decimal_precision, c_weight);
#ifdef DEBUG_BT

#ifdef ENGLISH
  Serial.print("手冲模式 ");  //pourover mode
  Serial.print("原重:");      //raw weight
  Serial.print(f_weight_adc);
  Serial.print(",平滑:");  //smoothed weight
  Serial.print(f_weight_smooth);
  Serial.print(",显示:");  //displaed weight
  Serial.println(f_displayedValue);
  SerialBT.print("手冲模式 ");  //same for serialbt
  SerialBT.print("原重:");
  SerialBT.print(f_weight_adc);
  SerialBT.print(",平滑:");
  SerialBT.print(f_weight_smooth);
  SerialBT.print(",显示:");
  SerialBT.println(f_displayedValue);
#endif
#ifdef ENGLISH
  Serial.print("Pour Over mode ");  //pourover mode
  Serial.print("Raw:");             //raw weight
  Serial.print(f_weight_adc);
  Serial.print(",Smoothed:");  //smoothed weight
  Serial.print(f_weight_smooth);
  Serial.print(",Displayed:");  //displaed weight
  Serial.println(f_displayedValue);
  SerialBT.print("Pour Over mode ");  //same for serialbt
  SerialBT.print("Raw:");
  SerialBT.print(f_weight_adc);
  SerialBT.print(",Smoothed:");
  SerialBT.print(f_weight_smooth);
  SerialBT.print(",Displayed:");
  SerialBT.println(f_displayedValue);
#endif
#endif
}

void espressoScale() {
  if (f_weight_dose < 3)
    f_weight_dose = INPUTCOFFEEESPRESSO;
  static bool newDataReady = 0;
  static bool scaleStable = 0;
  if (scale.update()) newDataReady = true;
  if (newDataReady) {
    if (millis() > t_last_weight_adc + i_serial_print_interval) {
      f_weight_adc = scale.getData();
      newDataReady = 0;
      t_last_weight_adc = millis();
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
      if (millis() > t_flow_rate + 1000) {
        f_flow_rate = f_displayedValue - f_flow_rate_last_weight;
        if (f_flow_rate < 0)
          f_flow_rate = 0;
        dtostrf(f_flow_rate, 7, i_decimal_precision, c_flow_rate);
        f_flow_rate_last_weight = f_displayedValue;
        t_flow_rate = millis();
      }
      dtostrf(f_displayedValue, 7, 1, c_weight);
      if (b_weight_in_serial == true)
        Serial.println(trim(c_weight));

      if (millis() > t_scale_stable + scaleStableInterval) {
        //稳定判断
        t_scale_stable = millis();  //重量稳定打点
        //get the time stamp for a stable scale.
        if (abs(aWeight - f_weight_smooth) < aWeightDiff) {
          scaleStable = true;  //称已经稳定
#ifdef DEBUG_BT
          Serial.println("称已经稳定");
          //scale is steady
          SerialBT.println("称已经稳定");
#endif
          aWeight = f_weight_smooth;  //稳定重量aWeight
          //smoothed data
          if (millis() > t_auto_tare + autoTareInterval) {
            if (t_extraction_begin > 0 && tareCounter > 3) {
              //t_extraction_begin>0 已经开始萃取 tareCounter>3 忽略前期tare时不稳定
              // if the stable reading number diviation is greater than 3
              if (t_extraction_last_drop == 0) {    //没有给t_extraction_last_drop计时过
                t_extraction_last_drop = millis();  //萃取完成打点
              }
              if (t_extraction_last_drop - t_extraction_first_drop < i_extraction_minimium_timer * 1000) {  //前7秒不判断停止计时 因此继续计时
                t_extraction_last_drop = 0;
              } else if (b_f_ready_to_brew) {
                //正常过程 最终下液到稳定时间大于5秒
                //normal brewing, the last drop to stalbe scale is greater than 5 second
                stopWatch.stop();
//萃取完成 单次固定液重
#ifdef DEBUG_BT
                Serial.println("萃取完成");
                SerialBT.println("萃取完成");
#endif
                buzzer.beep(3, 50);
                b_f_ready_to_brew = false;
              }
            }
            if (stopWatch.elapsed() == 0) {
              //秒表没有运行
              t_auto_tare = millis();                                       //自动清零计时打点
              if (f_displayedValue > 30 && b_f_minus_container == false) {  //大于30g说明放了杯子 3g是纸杯
                //后面的判断避免手柄模式超过30g对放杯感应产生干扰
                scale.tare();
                buzzer.beep(1, 100);
                tareCounter = 0;
                t_extraction_last_drop = 0;
                t_extraction_first_drop = 0;
                t_extraction_first_drop_num = 0;
                if (b_f_ready_to_brew) {
                  //已经准备冲煮状态 才开始计时
                  stopWatch.reset();
                  stopWatch.start();
                } else {
                  //没有准备好冲煮 则第一次清零不计时 并进入准备冲煮状态
                  b_f_ready_to_brew = true;
                }
                scaleStable = false;
                t_extraction_begin = millis();
                //Serial.println(F("正归零 开始计时 取消稳定"));
              }
              //时钟为零，负重量稳定后归零，时钟不变
              if (f_displayedValue < -0.5) {  //负重量状态
                scale.tare();
                //负归零后 进入准备冲煮状态 【下次】放了杯子后 清零并计时
                b_f_ready_to_brew = true;
              }
            }
            atWeight = f_displayedValue;
          }
        } else {  //称不稳定
          scaleStable = false;
          if (t_extraction_begin > 0) {
            //过滤tare环节的不稳
            if (tareCounter <= 3)
              tareCounter++;  ///tare后 遇到前2次不稳 视为稳定
            else {
              //tareCounter > 3 //视为开始萃取
              //萃取开始 下液重量开始计算
              //w1 = f_weight_adc;
              if (t_extraction_first_drop == 0) {
                t_extraction_first_drop = millis();  //第一滴下液
              }
            }
          }
          aWeight = f_displayedValue;
          //不稳 为负 停止计时
          //stopWatch.stop();
        }
      }
    }
  }

  //记录咖啡粉时，将重量固定为0
  if (scale.getTareStatus()) {
    b_f_minus_container = false;
    buzzer.beep(2, 50);
    //Serial.println("beep2 espressoScale");
    b_f_ready_to_brew = true;
    b_f_weight_quick_zero = false;
  }
  if (b_f_weight_quick_zero)
    f_displayedValue = 0.0;

  float ratio_temp = f_displayedValue / f_weight_dose;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (f_weight_dose < 0.1)
    ratio_temp = 0.0;

  dtostrf(f_displayedValue, 7, 1, c_weight);
  dtostrf(ratio_temp, 7, 1, c_brew_ratio);
#ifdef DEBUG_BT
#ifdef CHINESE
  Serial.print("意式模式 ");
  Serial.print("原重:");
  Serial.print(f_weight_adc);
  Serial.print(",平滑:");
  Serial.print(f_weight_smooth);
  Serial.print(",显示:");
  Serial.println(f_displayedValue);
  SerialBT.print("意式模式 ");
  SerialBT.print("原重:");
  SerialBT.print(f_weight_adc);
  SerialBT.print(",平滑:");
  SerialBT.print(f_weight_smooth);
  SerialBT.print(",显示:");
  SerialBT.println(f_displayedValue);
#endif
#ifdef ENGLISH
  Serial.print("Espresso Mode ");
  Serial.print("Raw:");
  Serial.print(f_weight_adc);
  Serial.print(",Smoothed:");
  Serial.print(f_weight_smooth);
  Serial.print(",Displayed:");
  Serial.println(f_displayedValue);
  SerialBT.print("Espresso Mode ");
  SerialBT.print("Raw:");
  SerialBT.print(f_weight_adc);
  SerialBT.print(",Smoothed:");
  SerialBT.print(f_weight_smooth);
  SerialBT.print(",Displayed:");
  SerialBT.println(f_displayedValue);
#endif
#endif
}

// float measureTemperature() {
//   int raw_value = analogRead(THERMISTOR_PIN);
//   // Convert the raw value to resistance
//   float voltage = raw_value * (3.3 / 4095.0);  // ESP32 has a 12-bit ADC, so 4095 is the maximum value
//   float resistance = SERIESRESISTOR * (3.3 / voltage - 1);

//   // Calculate the temperature in Celsius
//   float steinhart;
//   steinhart = resistance / NOMINAL_RESISTANCE;        // (R/Ro)
//   steinhart = log(steinhart);                         // ln(R/Ro)
//   steinhart /= BCOEFFICIENT;                          // 1/B * ln(R/Ro)
//   steinhart += 1.0 / (NOMINAL_TEMPERATURE + 273.15);  // + (1/To)
//   steinhart = 1.0 / steinhart - 273.15;               // invert and convert to C

//   // Apply a low-pass filter to smooth the temperature readings
//   static float filtered_temperature = steinhart;
//   filtered_temperature += FILTER_CONSTANT * (steinhart - filtered_temperature);
//   f_filtered_temperature = filtered_temperature;
//   return filtered_temperature;
// }

// float weightCompensation(float input) {
//   // float x = f_filtered_temperature - NOMINAL_TEMPERATURE;
//   // float result = input - (0.0023 * x * x * x - 0.0693 * x * x + 0.4252 * x - 0.0315);
//   // return result;
//   // -0.4436 -0.8131
//   float c = -0.4436;
//   if (f_temp_tare == 0)
//     f_temp_tare = NOMINAL_TEMPERATURE;
//   float result = input - c * (f_filtered_temperature - f_temp_tare);
//   return result;
// }

void pureScale() {
  static boolean newDataReady = 0;
  static boolean scaleStable = 0;
  float f_weight_adc_raw = 0;
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
    if (!b_f_minus_container_button) {
      //自动减去容器重量
      if (f_weight_container >= 1 && f_weight_smooth >= f_weight_container - NegativeTolerance && f_weight_smooth <= f_weight_container + PositiveTolerance) {
        // calculate difference between scale value and container value
        f_weight_smooth = f_weight_smooth - f_weight_container;
        b_f_minus_container = true;
      } else
        b_f_minus_container = false;
    } else if (f_weight_container >= 1) {
      //手动减去容器重量
      f_weight_smooth = f_weight_smooth - f_weight_container;
      b_f_minus_container = true;
    }
    // print smoothed reading
    newDataReady = false;
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

    f_weight_before_input = f_displayedValue;

    //串口输出原始重量读数
    Adafruit_Sensor *mpu_accel;
    sensors_event_t a;
    mpu_accel = mpu.getAccelerometerSensor();
    mpu_accel->getEvent(&a);

    dtostrf(f_displayedValue, 7, i_decimal_precision, c_weight);
    if (b_weight_in_serial == true)
      Serial.println(trim(c_weight));
#ifdef DEBUG_BT
#ifdef CHINESES
    Serial.print("称重模式 ");
    Serial.print("原重:");
    Serial.print(f_weight_adc);
    Serial.print(",平滑:");
    Serial.print(f_weight_smooth);
    Serial.print(",显示:");
    Serial.println(f_displayedValue);
    SerialBT.print("称重模式 ");
    SerialBT.print("原重:");
    SerialBT.print(f_weight_adc);
    SerialBT.print(",平滑:");
    SerialBT.print(f_weight_smooth);
    SerialBT.print(",显示:");
    SerialBT.println(f_displayedValue);
    Serial.print("Gyro Z:");
    Serial.println(a.acceleration.z);
#endif
#ifdef ENGLISH
    Serial.print("Weighing Mode ");
    Serial.print("Raw:");
    Serial.print(f_weight_adc);
    Serial.print(",Smoothed:");
    Serial.print(f_weight_smooth);
    Serial.print(",Displayed:");
    Serial.println(f_displayedValue);
    SerialBT.print("Weighing Mode ");
    SerialBT.print("Raw:");
    SerialBT.print(f_weight_adc);
    SerialBT.print(",Smoothed:");
    SerialBT.print(f_weight_smooth);
    SerialBT.print(",Displayed:");
    SerialBT.println(f_displayedValue);
    Serial.print("Gyro Z:");
    Serial.println(a.acceleration.z);
#endif
#endif
  }
  if (scale.getTareStatus()) {
    buzzer.beep(2, 50);
    b_f_weight_quick_zero = false;
  }
  //记录咖啡粉时，将重量固定为0
  if (b_f_weight_quick_zero)
    f_displayedValue = 0.0;

  float ratio_temp = f_displayedValue / f_weight_dose;
  if (ratio_temp < 0)
    ratio_temp = 0.0;
  if (f_weight_dose < 0.1)
    ratio_temp = 0.0;
  dtostrf(ratio_temp, 7, i_decimal_precision, c_brew_ratio);
  // if (millis() - t_temp > 500) {
  //   // Serial.print("temperature:");
  //   Serial.print(f_filtered_temperature);
  //   Serial.print("\t");
  //   // Serial.print("weightCompensation:");
  //   Serial.print(f_weight_smooth);
  //   Serial.print("\t");
  //   // Serial.print("f_weight_smooth:");
  //   Serial.println(f_displayedValue);
  //   t_temp = millis();
  // }
}

#if defined(DEBUG) && defined(CHECKBATTERY)
//debug信息
char c_adc_bat[10] = "";
char c_adc_usb[10] = "";
char c_raw[10] = "";
char c_adc_voltage_bat[10] = "";
char c_adc_voltage_usb[10] = "";
char c_real_voltage_bat[10] = "";
char c_real_voltage_usb[10] = "";
char c_show_voltage_bat[10] = "";
char c_show_voltage_usb[10] = "";
char c_up_hr[10] = "";
char c_up_min[10] = "";
char c_up_sec[10] = "";
char c_battery_life_hr[10] = "";
char c_battery_life_min[10] = "";
char c_battery_life_sec[10] = "";
char c_up_battery[10] = "";
long t_adc_interval = 0;


void debugData() {
  if (millis() > t_oled_refresh) {
    //达到设定的oled刷新频率后进行刷新
    t_oled_refresh = millis();
    u8g2.firstPage();
    do {
      u8g2.setFontDirection(0);
#ifdef ROTATION_180
      u8g2.setDisplayRotation(U8G2_R2);
#else
      u8g2.setDisplayRotation(U8G2_R0);
#endif

      u8g2.setFont(FONT_S);
      int x = 0;
      int y = u8g2.getMaxCharHeight() - 3;
      dtostrf(f_up_battery, 7, 2, c_up_battery);
      dtostrf(analogRead(BATTERY_LEVEL), 7, 0, c_adc_bat);
      dtostrf(analogRead(USB_LEVEL), 7, 0, c_adc_usb);
      dtostrf(analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1), 7, 2, c_adc_voltage_bat);
      dtostrf(analogRead(USB_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1), 7, 2, c_adc_voltage_usb);
      dtostrf(analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor / 2, 7, 2, c_real_voltage_bat);
      dtostrf(analogRead(USB_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor / 2, 7, 2, c_real_voltage_usb);
      dtostrf(analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor, 7, 2, c_show_voltage_bat);
      dtostrf(analogRead(USB_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor, 7, 2, c_show_voltage_usb);

      if (millis() - t_adc_interval > 4000) {
        float f_now_battery = analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
        float f_v_drop_rate = (f_up_battery - f_now_battery) / (millis() - t_up_battery);
        long t_battery_life = (f_now_battery - 3.3) / f_v_drop_rate;
        int i_battery_life_hr = numberOfHours(t_battery_life / 1000);
        int i_battery_life_min = numberOfMinutes(t_battery_life / 1000);
        int i_battery_life_sec = numberOfSeconds(t_battery_life / 1000);
        dtostrf(i_battery_life_hr, 7, 0, c_battery_life_hr);
        dtostrf(i_battery_life_min, 7, 0, c_battery_life_min);
        dtostrf(i_battery_life_sec, 7, 0, c_battery_life_sec);
        dtostrf(numberOfHours(millis() / 1000), 7, 0, c_up_hr);
        // dtostrf(numberOfMinutes(millis() / 1000), 7, 0, c_up_min);
        // dtostrf(numberOfSeconds(millis() / 1000), 7, 0, c_up_sec);

        t_adc_interval = millis();
      }
      // if (scale.update()) {
      //   f_weight_adc = scale.getData();
      //   dtostrf(f_weight_adc, 7, 1, c_raw);
      // }
      u8g2.drawUTF8(AR(trim(c_up_battery)), y, trim(c_up_battery));
      u8g2.drawUTF8(x, y, trim(c_adc_bat));
      u8g2.drawUTF8(x, y * 2, trim(c_adc_voltage_bat));
      u8g2.drawUTF8(x, y * 3, trim(c_real_voltage_bat));
      u8g2.drawUTF8(x, y * 4, trim(c_show_voltage_bat));
      u8g2.drawUTF8(AC(trim(c_adc_usb)), y, trim(c_adc_usb));
      u8g2.drawUTF8(AC(trim(c_adc_usb)), y * 2, trim(c_adc_voltage_usb));
      u8g2.drawUTF8(AC(trim(c_adc_usb)), y * 3, trim(c_real_voltage_usb));
      u8g2.drawUTF8(AC(trim(c_adc_usb)), y * 4, trim(c_show_voltage_usb));
      //u8g2.drawUTF8(AR(trim(c_raw)), y, trim(c_raw));
      u8g2.drawUTF8(AR(trim(c_battery_life_hr)), y * 2, trim(c_battery_life_hr));
      u8g2.drawUTF8(AR(trim(c_battery_life_min)), y * 3, trim(c_battery_life_min));
      u8g2.drawUTF8(AR(trim(c_battery_life_sec)), y * 4, trim(c_battery_life_sec));
      u8g2.drawUTF8(90, y * 2, trim(c_up_hr));
      u8g2.drawUTF8(90, y * 3, trim(c_up_min));
      u8g2.drawUTF8(90, y * 4, trim(c_up_sec));

    } while (u8g2.nextPage());
  }
}
#endif  // defined(DEBUG) && defined(CHECKBATTERY)

void serialCommand() {
  if (Serial.available()) {
    String inputString = Serial.readStringUntil('\n');
    inputString.trim();

    if (inputString.startsWith("welcome ")) {
      //strcpy(str_welcome, inputString.substring(8).c_str());
      EEPROM.put(i_addr_welcome, inputString.substring(8));
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputString.startsWith("cp ")) {  //手冲粉量
      INPUTCOFFEEPOUROVER = inputString.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputString.startsWith("v")) {  //电压
      Serial.print("Battery Voltage:");
      Serial.println(getVoltage(BATTERY_PIN));
    }

    if (inputString.startsWith("ce ")) {  //意式粉量
      INPUTCOFFEEESPRESSO = inputString.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputString.startsWith("ct ")) {  //容器重量
      f_weight_container = inputString.substring(3).toFloat();
      EEPROM.put(i_addr_container, f_weight_container);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputString.startsWith("cv ")) {  //校准值
      f_calibration_value = inputString.substring(3).toFloat();
      EEPROM.put(i_addr_calibration_value, f_calibration_value);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }


    if (inputString.startsWith("reset")) {  //重启
      reset();
    }

    // if (inputString.startsWith("sample")) {
    //   b_f_set_sample = true;
    //   i_sample = 0;  //读取失败 默认值为3 对应sample为8
    //   i_sample_step = 0;
    //   setSample();
    // }

    if (inputString.startsWith("cal")) {  //校准
      b_f_calibration = true;             //让按钮进入校准状态3
      cal();                              //无有效读取，进入校准模式
    }

    if (inputString.startsWith("tare")) {
      buttonTare_Clicked();
    }

    if (inputString.startsWith("set")) {
      buttonSet_Clicked();
    }

    if (inputString.startsWith("beep")) {  //蜂鸣器
      b_f_beep = !b_f_beep;
      EEPROM.put(i_addr_beep, b_f_beep);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }


// Send the updated values via USB serial
#ifdef CHINESE
    Serial.print("手冲默认粉重:");
    Serial.print(INPUTCOFFEEPOUROVER);
    Serial.print("g\t意式默认粉重:");
    Serial.print(INPUTCOFFEEESPRESSO);
    Serial.print("g\t容器重量:");
    Serial.print(f_weight_container);
    Serial.print("g\t蜂鸣器状态:");
    if (b_f_beep)
      Serial.println("打开");
    else
      Serial.println("关闭");
#endif
#ifdef ENGLISH
    Serial.print("Pour Over default ground weight:");
    Serial.print(INPUTCOFFEEPOUROVER);
    Serial.print("g\tEspresso default ground weight:");
    Serial.print(INPUTCOFFEEESPRESSO);
    Serial.print("g\tContainer Weight:");
    Serial.print(f_weight_container);
    Serial.print("g\tBuzzer:");
    if (b_f_beep)
      Serial.println("On");
    else
      Serial.println("Off");
#endif
  }

  if (SerialBT.available()) {
    String inputStringBT = SerialBT.readStringUntil('\n');
    inputStringBT.trim();

    if (inputStringBT.startsWith("welcome ")) {
      //strcpy(str_welcome, inputStringBT.substring(8).c_str());
      EEPROM.put(i_addr_welcome, inputStringBT.substring(8));
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputStringBT.startsWith("cp ")) {  //手冲粉量
      INPUTCOFFEEPOUROVER = inputStringBT.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputStringBT.startsWith("v")) {  //电压
      SerialBT.print("Battery Voltage:");
      SerialBT.println(getVoltage(BATTERY_PIN));
    }

    if (inputStringBT.startsWith("ce ")) {  //意式粉量
      INPUTCOFFEEESPRESSO = inputStringBT.substring(3).toFloat();
      EEPROM.put(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputStringBT.startsWith("ct ")) {  //容器重量
      f_weight_container = inputStringBT.substring(3).toFloat();
      EEPROM.put(i_addr_container, f_weight_container);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }

    if (inputStringBT.startsWith("cv ")) {  //校准值
      f_calibration_value = inputStringBT.substring(3).toFloat();
      EEPROM.put(i_addr_calibration_value, f_calibration_value);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }


    if (inputStringBT.startsWith("reset")) {  //重启
      reset();
    }

    // if (inputStringBT.startsWith("sample")) {
    //   b_f_set_sample = true;
    //   i_sample = 0;  //读取失败 默认值为3 对应sample为8
    //   i_sample_step = 0;
    //   setSample();
    // }

    if (inputStringBT.startsWith("cal")) {  //校准
      b_f_calibration = true;               //让按钮进入校准状态3
      cal();                                //无有效读取，进入校准模式
    }

    if (inputStringBT.startsWith("tare")) {
      buttonTare_Clicked();
    }

    if (inputStringBT.startsWith("set")) {
      buttonSet_Clicked();
    }

    if (inputStringBT.startsWith("beep")) {  //蜂鸣器
      b_f_beep = !b_f_beep;
      EEPROM.put(i_addr_beep, b_f_beep);
#if defined(ESP8266) || defined(ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_MBED_RP2040)
      EEPROM.commit();
#endif
    }


// Send the updated values via USB serial
#ifdef CHINESE
    SerialBT.print("手冲默认粉重:");
    SerialBT.print(INPUTCOFFEEPOUROVER);
    SerialBT.print("g\t意式默认粉重:");
    SerialBT.print(INPUTCOFFEEESPRESSO);
    SerialBT.print("g\t容器重量:");
    SerialBT.print(f_weight_container);
    SerialBT.print("g\t蜂鸣器状态:");
    if (b_f_beep)
      SerialBT.println("打开");
    else
      SerialBT.println("关闭");
#endif
#ifdef ENGLISH
    SerialBT.print("Pour Over default ground weight:");
    SerialBT.print(INPUTCOFFEEPOUROVER);
    SerialBT.print("g\tEspresso default ground weight:");
    SerialBT.print(INPUTCOFFEEESPRESSO);
    SerialBT.print("g\tContainer weight:");
    SerialBT.print(f_weight_container);
    SerialBT.print("g\tBuzzer:");
    if (b_f_beep)
      SerialBT.println("On");
    else
      SerialBT.println("Off");
#endif
  }
}

void loop() {
  //Serial.println(getDoubleClickDelay());
  power_off(15);  //power off after 15 minutes

  serialCommand();
  if (deviceConnected) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastWeightNotifyTime >= weightNotifyInterval) {
      // Save the last time you sent the weight notification
      lastWeightNotifyTime = currentMillis;

      byte data[7];
      float weight = scale.getData();
      byte weightByte1, weightByte2;

      encodeWeight(weight, weightByte1, weightByte2);

      data[0] = modelByte;
      data[1] = 0xCE;  // Type byte for weight stable
      data[2] = weightByte1;
      data[3] = weightByte2;
      // Fill the rest with dummy data or real data as needed
      data[4] = 0x00;
      data[5] = 0x00;
      data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

      pReadCharacteristic->setValue(data, 7);
      pReadCharacteristic->notify();
    }
  }
  //
  // if (SerialBT.available()) {
  //   String inputString = SerialBT.readStringUntil('\n');
  //   inputString.trim();

  //   if (inputString.startsWith("cp ")) {
  //     INPUTCOFFEEPOUROVER = inputString.substring(3).toFloat();
  //     EEPROM.put(INPUTCOFFEEPOUROVER_ADDRESS, INPUTCOFFEEPOUROVER);
  //   } else if (inputString.startsWith("ce ")) {
  //     INPUTCOFFEEESPRESSO = inputString.substring(3).toFloat();
  //     EEPROM.put(INPUTCOFFEEESPRESSO_ADDRESS, INPUTCOFFEEESPRESSO);
  //   }

  //   // Send the updated values via Bluetooth
  //   SerialBT.print("手冲默认粉重");
  //   SerialBT.println(INPUTCOFFEEPOUROVER);
  //   SerialBT.print("意式默认粉重");
  //   SerialBT.println(INPUTCOFFEEESPRESSO);
  // }

  // measureTemperature();
  buttonSet.check();
#if defined(FOUR_BUTTON) || defined(FIVE_BUTTON)
  buttonPlus.check();
  buttonMinus.check();
#endif  //extra button for four and five buttons
  buttonTare.check();
#ifdef FIVE_BUTTON
  buttonPower.check();
#endif  //FIVE_BUTTON

#ifdef GYROFACEUP
  if (gyro_z() > 5)
    power_off_gyro(-1);
#endif
#ifdef GYROFACEDOWN
  if (gyro_z() < -5)
    power_off_gyro(-1);
#endif
  power_off_gyro(10);

#if defined(DEBUG) && defined(CHECKBATTERY)
  debugData();
#endif  //DEBUG
#ifdef CHECKBATTERY
  checkBattery();
#endif
  if (b_f_extraction) {
    if (b_f_mode)
      espressoScale();
    else
      pourOverScale();
  } else {
    pureScale();
  }
  updateOled();
  if (b_f_espnow) {
    updateEspnow();
  }
}

void updateOled() {

  if (millis() > t_oled_refresh + i_oled_print_interval) {
    //达到设定的oled刷新频率后进行刷新
    t_oled_refresh = millis();
    int x = 0;
    int y = 0;
    char c_oled_ratio[30];
    sprintf(c_oled_ratio, "1:%s", trim(c_brew_ratio));
    char c_coffee_powder[30];
    dtostrf(f_weight_dose, 7, i_decimal_precision, c_coffee_powder);
    u8g2.firstPage();
    if (b_f_extraction) {
      do {
        if (i_display_rotation == 0) {
          u8g2.setFontDirection(0);
#ifdef ROTATION_180
          u8g2.setDisplayRotation(U8G2_R2);
#else
          u8g2.setDisplayRotation(U8G2_R0);
#endif
          u8g2.setFont(FONT_EXTRACTION);

          //实时重量
          x = Margin_Left;
          y = u8g2.getMaxCharHeight() + Margin_Top - 5;
          u8g2.drawUTF8(x, y, trim(c_weight));

          //时钟
          if (b_f_mode) {
            u8g2.drawUTF8(AR(sec2sec(stopWatch.elapsed())), y, sec2sec(stopWatch.elapsed()));
            if (t_extraction_first_drop > 0 && t_extraction_first_drop - t_extraction_begin > 0) {  //有下液了
              t_extraction_first_drop_num = (t_extraction_first_drop - t_extraction_begin) / 1000;
              u8g2.drawUTF8(70, y, sec2sec(t_extraction_first_drop_num));
            }
          } else {
            u8g2.drawUTF8(AR(sec2minsec(stopWatch.elapsed())), y, sec2minsec(stopWatch.elapsed()));
          }
          y = y + u8g2.getMaxCharHeight() + 2;
          u8g2.drawUTF8(0, y + 1, trim(c_flow_rate));

          x = Margin_Left;
          y = LCDHeight - Margin_Bottom;
          u8g2.drawUTF8(x, y, trim(c_coffee_powder));
          u8g2.drawUTF8(AR(trim(c_oled_ratio)), y, trim(c_oled_ratio));
#ifdef CHECKBATTERY
          u8g2.setFontDirection(1);
          u8g2.setFont(FONT_BATTERY);
          if (b_f_is_charging) {
            u8g2.drawUTF8(108, 29, "6");
            //Serial.println("ischarging");
          } else {
            if (millis() > t_batteryRefresh + batteryRefreshTareInterval) {
              c_batteryTemp = c_battery;
              t_batteryRefresh = millis();
            }
            u8g2.drawUTF8(108, 29, c_batteryTemp);
          }
#endif
#ifdef DEBUG

          u8g2.setFontDirection(0);
          u8g2.setFont(FONT_S);
          float batteryVoltage = analogRead(BATTERY_LEVEL) * f_vref / 1023;
          char c_votage[10];
          String(batteryVoltage).toCharArray(c_votage, 10);
          u8g2.drawUTF8(AR(c_votage), 64, trim(c_votage));
          Serial.print("v");
          Serial.println(c_votage);
#endif  //DEBUG
        }
        //////////////////旋转效果
        if (i_display_rotation == 1) {
#ifdef ROTATION_180
          u8g2.setDisplayRotation(U8G2_R3);
#else
          u8g2.setDisplayRotation(U8G2_R1);
#endif
          //00 battery
#ifdef CHECKBATTERY
          u8g2.setFontDirection(1);
          u8g2.setFont(FONT_BATTERY);
          if (b_f_is_charging) {
            u8g2.drawStr(64 - 20, 6, "6");
            Serial.println("ischarging");
          } else {
            if (millis() > t_batteryRefresh + batteryRefreshTareInterval) {
              c_batteryTemp = c_battery;
              t_batteryRefresh = millis();
            }
            u8g2.drawUTF8(64 - 20, 6, c_batteryTemp);
          }
#endif
          u8g2.setFontDirection(0);
          u8g2.setFont(FONT_S);
          x = Margin_Left;
          y = u8g2.getMaxCharHeight() + Margin_Top - 5 + 1;
          //00 ESP
          if (b_f_mode)
            u8g2.drawUTF8(0, y, TEXT_ESPRESSO);
          else
            u8g2.drawUTF8(0, y, TEXT_POUROVER);

          u8g2.setFont(FONT_EXTRACTION);
          //01 粉重
          y = y + u8g2.getMaxCharHeight() + 1;
          u8g2.drawUTF8(x, y, trim(c_coffee_powder));
          //02 重量
          y = y + u8g2.getMaxCharHeight() + 1;
          u8g2.drawUTF8(0, y, trim(c_weight));
          //03 时间
          y = y + u8g2.getMaxCharHeight() + 1;
          if (b_f_mode) {
            u8g2.drawUTF8(0, y, sec2sec(stopWatch.elapsed()));

            if (t_extraction_first_drop > 0 && t_extraction_first_drop - t_extraction_begin > 0) {  //有下液了
              t_extraction_first_drop_num = (t_extraction_first_drop - t_extraction_begin) / 1000;
              u8g2.drawUTF8(AR(sec2sec(t_extraction_first_drop_num)), y, sec2sec(t_extraction_first_drop_num));
            }
          } else {
            u8g2.drawUTF8(0, y, sec2minsec(stopWatch.elapsed()));
          }

          y = y + u8g2.getMaxCharHeight() + 1;
          u8g2.drawUTF8(0, y, trim(c_flow_rate));

          //04 粉水比
          y = y + u8g2.getMaxCharHeight();

          //u8g2.setFont(FONT_M);
          x = Margin_Left;
          y = LCDHeight - Margin_Bottom;

          u8g2.drawUTF8(0, y, trim(c_oled_ratio));
        }
      } while (u8g2.nextPage());
    } else {
      ////////////////////////////////////
      //纯称重
      do {
#ifdef ROTATION_180
        u8g2.setDisplayRotation(U8G2_R2);
#else
        u8g2.setDisplayRotation(U8G2_R0);
#endif
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        x = 0;
        y = 12;
        if (b_f_mode) {
          if (b_f_minus_container) {
            if (!b_f_minus_container_button)
              u8g2.drawUTF8(x, y, TEXT_ESPRESSO_AUTO_TARE_CONTAINER);
            else
              u8g2.drawUTF8(x, y, TEXT_ESPRESSO_MANUAL_TARE_CONTAINER);
          } else
            u8g2.drawUTF8(x, y, TEXT_ESPRESSO);
        } else {
          if (b_f_minus_container) {
            if (!b_f_minus_container_button)
              u8g2.drawUTF8(x, y, TEXT_POUROVER_AUTO_TARE_CONTAINER);
            else
              u8g2.drawUTF8(x, y, TEXT_POUROVER_MANUAL_TARE_CONTAINER);
          } else
            u8g2.drawUTF8(x, y, TEXT_POUROVER);
        }

        u8g2.setFont(FONT_L);
        //FONT_L_HEIGHT = u8g2.getMaxCharHeight();
        x = AC(trim(c_weight));
        y = AM();
        u8g2.drawUTF8(x, y + 3, trim(c_weight));
#ifdef CHECKBATTERY
        u8g2.setFontDirection(1);
        u8g2.setFont(FONT_BATTERY);
        if (b_f_is_charging) {
          u8g2.drawUTF8(108, 3, "6");
        } else {
          if (millis() > t_batteryRefresh + batteryRefreshTareInterval) {
            c_batteryTemp = c_battery;
            t_batteryRefresh = millis();
          }
          u8g2.drawUTF8(108, 3, c_batteryTemp);
        }
#endif
        u8g2.setFontDirection(0);
#ifdef DEBUG_BATTERY
        if (b_f_debug_battery) {
          u8g2.setFont(FONT_S);
          float batteryVoltage = analogRead(BATTERY_LEVEL) * f_vref / (pow(2, ADC_BIT) - 1) * f_divider_factor;
          char c_votage[10];
          String(batteryVoltage).toCharArray(c_votage, 10);
          u8g2.drawUTF8(AR(c_votage), 64, trim(c_votage));
          Serial.print("v");
          Serial.println(c_votage);
        }
#endif  //DEBUG_BATTERY
      } while (u8g2.nextPage());
    }
  }
}
