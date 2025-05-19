#ifndef BLE_H
#define BLE_H
#include "declare.h"


// #include <BluetoothSerial.h>
// BluetoothSerial SerialBT;
enum BleState {
  DISCONNECTED,
  CONNECTED
};
BleState bleState = DISCONNECTED;
const unsigned long HEARTBEAT_TIMEOUT = 5000;  // 5 seconds

//functions
void sendBleVoltage();
void sendBleGyro();
uint16_t connId = 0;


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
    connId = pServer->getConnId();  // 保存连接 ID
    t_heartBeat = millis();
    bleState = CONNECTED;
    deviceConnected = true;
    b_beep = false;  //disable buzzer once when connected, and wait for ble command to enable it
    Serial.println("Device connected");
  }

  void onDisconnect(BLEServer *pServer) {
    connId = 0;

    deviceConnected = false;
    bleState = DISCONNECTED;

    //Serial.println("ble 01");
    EEPROM.get(i_addr_beep, b_beep);  //read buzzer state after disconnect
    //Serial.println("ble 02");

    Serial.println("Device disconnected, restarting advertising...");

    // re-advertising after connection lost
    u8g2.setPowerSave(0);
    //Serial.println("ble 3");

    delay(100);  // 推荐加一点延时再开始广播
    pAdvertising->start();
    //Serial.println("ble 5");
  }
};
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
        
        sofronio edit 2024-08-31
        031A Start calibration
        031B Start WiFi OTA
        031C 00 Buzzer off
        031C 01 Buzzer on

        03 2A 00 Power off by button
        03 2A 01 Power off by low power        
*/
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

  void onWrite(BLECharacteristic *pWriteCharacteristic) {
    //this is what the esp32 received via ble
    Serial.print("Timer");
    Serial.print(millis());
    Serial.print(" onWrite counter:");
    Serial.print(i_onWrite_counter++);
    Serial.print(" ");

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
      Serial.print(" ");
      if (data[0] == 0x03) {
        //check if it's a decent scale message
        if (data[1] == 0x0F) {
          //taring
          if (validateChecksum(data, len)) {
            Serial.println("Valid checksum for tare operation. Taring");
          } else {
            Serial.println("Invalid checksum for tare operation.");
          }
          b_tareByBle = true;
          t_tareByBle = millis();
          if (data[5] == 0x00) {
            /*
            Tare the scale by sending "030F000000000C" (old version, disables heartbeat)
            Tare the scale by sending "030F000000010D" (new version, leaves heartbeat as set)
            */
            b_requireHeartBeat = false;
            Serial.println("*** Heartbeat detection Off ***");
          }
          if (data[5] == 0x01) {
            /*
            Tare the scale by sending "030F000000000C" (old version, disables heartbeat)
            Tare the scale by sending "030F000000010D" (new version, leaves heartbeat as set)
            */
            Serial.print("*** Heartbeat detection remained ");
            if (b_requireHeartBeat)
              Serial.print("On");
            else
              Serial.print("Off");
            Serial.println(" ***");
          }
        } else if (data[1] == 0x0A) {
          if (data[2] == 0x00) {
            Serial.println("LED off detected. Turn off OLED.");
            u8g2.setPowerSave(1);
            b_u8g2Sleep = true;
          } else if (data[2] == 0x01) {
            Serial.println("LED on detected. Turn on OLED.");
            u8g2.setPowerSave(0);
            b_u8g2Sleep = false;
            if (data[5] == 0x00) {
              b_requireHeartBeat = false;
              Serial.println("*** Heartbeat detection Off ***");
            }
            if (data[5] == 0x01) {
              Serial.print("*** Heartbeat detection remained ");
              if (b_requireHeartBeat)
                Serial.print("On");
              else
                Serial.print("Off");
              Serial.println(" ***");
            }
          } else if (data[2] == 0x02) {
            Serial.println("Power off detected.");
            shut_down_now_nobeep();
          } else if (data[2] == 0x03) {
            if (data[3] == 0x01) {
              Serial.println("Start Low Power Mode.");
              u8g2.setContrast(0);
            } else if (data[3] == 0x00) {
              Serial.println("Exit low power mode.");
              u8g2.setContrast(255);
            } else if (data[3] == 0xFF) {
              if (data[4] == 0xFF) {
                if (data[5] == 0x00) {
                  if (data[6] == 0x0A) {
                    t_heartBeat = millis();
                    Serial.print("*** Heartbeat at ");
                    Serial.print(t_heartBeat);
                    Serial.println(" ***");
                  }
                }
              }
            }
          } else if (data[2] == 0x04) {
            if (data[3] == 0x01) {
              Serial.println("Start Soft Sleep.");
              u8g2.setPowerSave(1);
              b_softSleep = true;
              digitalWrite(PWR_CTRL, LOW);
              digitalWrite(ACC_PWR_CTRL, LOW);
            } else if (data[3] == 0x00) {
              Serial.println("Exit Soft Sleep.");
              digitalWrite(PWR_CTRL, HIGH);
              digitalWrite(ACC_PWR_CTRL, HIGH);
              u8g2.setPowerSave(0);
              b_softSleep = false;
            }
          }
        } else if (data[1] == 0x0B) {
          if (data[2] == 0x03) {
            Serial.println("Timer start detected.");
            stopWatch.reset();
            stopWatch.start();
          } else if (data[2] == 0x00) {
            Serial.println("Timer stop detected.");
            stopWatch.stop();
          } else if (data[2] == 0x02) {
            Serial.println("Timer zero detected.");
            stopWatch.reset();
          }
        } else if (data[1] == 0x1A) {
          if (data[2] == 0x00) {
            Serial.println("Manual Calibration via BLE");
            i_button_cal_status = 1;
            i_calibration = 0;
            b_calibration = true;
          } else if (data[2] == 0x01) {
            Serial.println("Smart Calibration via BLE");
            i_button_cal_status = 1;
            i_calibration = 1;
            b_calibration = true;
          }
        } else if (data[1] == 0x1B) {
          Serial.println("Start WiFi OTA");
          wifiUpdate();
        } else if (data[1] == 0x1C) {  //buzzer settings
          if (data[2] == 0x00) {
            Serial.println("Buzzer Off");
            b_beep = false;  // won't store into eeprom
          } else if (data[2] == 0x01) {
            Serial.println("Buzzer On");
            b_beep = true;  // won't store into eeprom
          } else if (data[2] == 0x02) {
            Serial.println("Buzzer Beep");
            buzzer.beep(1, BUZZER_DURATION);
          }
        } else if (data[1] == 0x1D) {  //Sample settings
          if (data[2] == 0x00) {
            scale.setSamplesInUse(1);
            Serial.print("Samples in use set to: ");
            Serial.println(scale.getSamplesInUse());
          } else if (data[2] == 0x01) {
            scale.setSamplesInUse(2);
            Serial.print("Samples in use set to: ");
            Serial.println(scale.getSamplesInUse());
          } else if (data[2] == 0x03) {
            scale.setSamplesInUse(4);
            Serial.print("Samples in use set to: ");
            Serial.println(scale.getSamplesInUse());
          }
        } else if (data[1] == 0x1E) {
          if (data[2] == 0x00) {
            //menu control
            if (data[3] == 0x00) {
              //hide menu
              Serial.println("Hide menu");
              b_menu = false;
            } else if (data[3] == 0x01) {
              //show menu
              Serial.println("Show menu");
              b_menu = true;
            }
          } else if (data[2] == 0x01) {
            //about info
            if (data[3] == 0x00) {
              //hide about info
              Serial.println("Hide about info");
              if (b_menu)
                b_showAbout = false;  //hide about info(code in menu) if it's enabled via menu
              else
                b_about = false;  //hide about info(code in loop) if it's enabled via ble command
            } else if (data[3] == 0x01) {
              //show about info
              Serial.println("Show about info");
              b_debug = false;
              b_about = true;
              b_menu = false;
            }
          } else if (data[2] == 0x02) {
            //debug info
            if (data[3] == 0x00) {
              Serial.println("Hide debug info");
              //hide debug info
              b_debug = false;
            } else if (data[3] == 0x01) {
              //show debug info
              Serial.println("Show debug info");
              b_about = false;
              b_debug = true;
              b_menu = false;
            }
          }
        } else if (data[1] == 0x1F) {
          reset();
        } else if (data[1] == 0x20) {
          if (data[2] == 0x00) {
            Serial.println("Weight by USB disabled");
            b_usbweight_enabled = false;
          } else if (data[2] == 0x01) {
            Serial.println("Weight by USB enabled");
            b_usbweight_enabled = true;
            if (len >= 4) {
              uint16_t interval = 100;
              uint8_t multiplier = data[3];
              if (multiplier < 1) multiplier = 1;
              if (multiplier > 50) multiplier = 50;  // 最大支持 5000ms

              interval = multiplier * 100;
              weightUsbNotifyInterval = interval;

              Serial.print("USB weight interval set to ");
              Serial.print(weightUsbNotifyInterval);
              Serial.println(" ms");
            }
          }
        } else if (data[1] == 0x21) {
          sendBleGyro();
        } else if (data[1] == 0x22) {
          sendBleVoltage();
        }
      }
    }
  }
};


void ble_init() {
  //turn on ble
  BLEDevice::init("Decent Scale");
  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // Create BLE Service
  pService = pServer->createService(SUUID_DECENTSCALE);
  pWriteCharacteristic = pService->createCharacteristic(
    CUUID_DECENTSCALE_WRITE,
    BLECharacteristic::PROPERTY_WRITE);
  pWriteCharacteristic->setCallbacks(new MyCallbacks());
  pReadCharacteristic = pService->createCharacteristic(
    CUUID_DECENTSCALE_READ,
    BLECharacteristic::PROPERTY_READ
      | BLECharacteristic::PROPERTY_NOTIFY);
  pReadCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  // Start advertising
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();

  Serial.println("Waiting for a client connection to notify...");
}

void disconnectBLE() {
  if (deviceConnected && pServer->getConnectedCount() > 0) {
    Serial.println("***No heartbeat for 5 seconds. Disconnecting BLE...***");
    pServer->disconnect(0);  // 只有一台设备时可直接用 0
  }
}

void sendBleVoltage() {
  if (deviceConnected) {
    Serial.print("Battery Voltage:");
    float voltage = getVoltage(BATTERY_PIN);
    Serial.print(voltage);
#ifndef ADS1115ADC
    int adcValue = analogRead(BATTERY_PIN);                              // Read the value from ADC
    float voltageAtPin = (adcValue / adcResolution) * referenceVoltage;  // Calculate voltage at ADC pin
    Serial.print("\tADC Voltage:");
    Serial.print(voltageAtPin);
    Serial.print("\tbatteryCalibrationFactor: ");
    Serial.print(f_batteryCalibrationFactor);
#endif
    Serial.print("\tlowBatteryCounterTotal: ");
    Serial.print(i_lowBatteryCountTotal);

    byte data[7];
    // float weight = scale.getData();
    byte voltageByte1, voltageByte2;

    encodeWeight(voltage, voltageByte1, voltageByte2);

    data[0] = modelByte;
    data[1] = 0x22;  // Type byte for gyro data
    data[2] = voltageByte1;
    data[3] = voltageByte2;
    // Fill the rest with dummy data or real data as needed
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

    pReadCharacteristic->setValue(data, 7);
    pReadCharacteristic->notify();
  }
}


void sendHeartBeat() {
  if (deviceConnected) {
    byte data[7];
    data[0] = modelByte;
    data[1] = 0x0A;  // Type byte for heart beat
    data[2] = 0x03;
    data[3] = 0xFF;
    data[4] = 0xFF;
    data[5] = 0x00;
    data[6] = 0x0A;

    pReadCharacteristic->setValue(data, 7);
    pReadCharacteristic->notify();
  }
}

void sendBleGyro() {
  if (deviceConnected) {
    byte data[7];
    // float weight = scale.getData();
    float gyro = gyro_z();
    byte gyroByte1, gyroByte2;

    encodeWeight(gyro, gyroByte1, gyroByte2);

    data[0] = modelByte;
    data[1] = 0x21;  // Type byte for gyro data
    data[2] = gyroByte1;
    data[3] = gyroByte2;
    // Fill the rest with dummy data or real data as needed
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

    pReadCharacteristic->setValue(data, 7);
    pReadCharacteristic->notify();
  }
}

void sendBleWeight() {
  if (deviceConnected) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastBleWeightNotifyTime >= weightBleNotifyInterval) {
      // Save the last time you sent the weight notification
      lastBleWeightNotifyTime = currentMillis;

      byte data[7];
      //float weight = scale.getData();
      float weight = f_displayedValue;
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
}

void sendBleButton(int buttonNumber, int buttonShortPress) {
  if (b_ble_enabled && deviceConnected) {
    //buttonNumber 1 for button O, 2 for button[]
    //buttonShortPress 1 for short press, 2 for long press
    byte data[7];

    data[0] = modelByte;
    data[1] = 0xAA;  // Type byte for weight stable
    data[2] = buttonNumber;
    data[3] = buttonShortPress;
    // 0 for release, 1 for click, 2 for long press
    // Fill the rest with dummy data or real data as needed
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

    pReadCharacteristic->setValue(data, 7);
    pReadCharacteristic->notify();
  }
}

void sendBlePowerOff(int i_reason) {
  if (b_ble_enabled && deviceConnected) {
    byte data[7];
    data[0] = modelByte;
    data[1] = 0x2A;
    switch (i_reason) {
      case 0:
        data[2] = 0x00;  //power off failed because it's disabled.
        break;
      case 1:
        data[2] = 0x10;  //power off from from "O" button double-click.
        break;
      case 2:
        data[2] = 0x11;  //power off from from "[]" button double-click.
        break;
      case 3:
        data[3] = 0x20;  //power off low battery.
        break;
      case 4:
        data[2] = 0x30;  //power off from gyro.
        break;
      default:
        data[2] = 0x00;  //Don't power off because i_reason is not valid.
        break;
    }
    data[3] = 0x00;
    data[4] = 0x00;
    data[5] = 0x00;
    data[6] = calculateXOR(data, 6);  // Last byte is XOR validation

    pReadCharacteristic->setValue(data, 7);
    pReadCharacteristic->notify();
  }
}
#endif