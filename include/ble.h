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
unsigned long t_lastDisconnectAttempt = 0;
unsigned long t_lastDisconnectAttemptNotice = 0;

//functions
void sendBleVoltage();
void sendBleLedResponse();
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif
uint16_t connId = 0xFFFF; // not set to 0 because 0 could be a valid client id.


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
    Serial.print("BLE connID is: ");
    Serial.println(connId);
    t_firstConnect = millis();
    bleState = CONNECTED;
    deviceConnected = true;
#ifdef BUZZER
    b_beep = false;  //disable buzzer once when connected, and wait for ble command to enable it
#endif
    Serial.println("Device connected");
  }

  void onDisconnect(BLEServer *pServer) {
    connId = 0xFFFF; // not set to 0 because 0 could be a valid client id

    deviceConnected = false;
    bleState = DISCONNECTED;

//Serial.println("ble 01");
#ifdef BUZZER
    EEPROM.get(i_addr_beep, b_beep);  //read buzzer state after disconnect
#endif
    //Serial.println("ble 02");

    Serial.println("Device disconnected, restarting advertising...");

    // re-advertising after connection lost
    u8g2.setPowerSave(0);

    delay(100);  // advertise after a short delay
    
    // maybe remove delay in future version or use somthing like this:
    /*
    void onDisconnect(BLEServer *pServer) {
    deviceConnected = false;
    restartAdvertising = true; // Just set the flag
    }

    void loop() {
        if (restartAdvertising) {
            delay(10); // Very short delay outside the callback is safer
            pAdvertising->start();
            restartAdvertising = false;
            Serial.println("Advertising restarted safely in loop");
        }
    }*/
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
    /* 
    millis() function returns an unsigned long. It will overflow (roll over back to zero) after approximately 49.7 days.
    This will NOT overflow for 290,000 years:
    uint64_t permanent_millis = esp_timer_get_time() / 1000; 
    */
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
            sendBleLedResponse();//include weight voltage version
          } else if (data[2] == 0x01) {
            Serial.println("LED on detected. Turn on OLED.");
            u8g2.setPowerSave(0);
            b_u8g2Sleep = false;
            sendBleLedResponse();//including weight voltage version
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
            stopWebServer();
            stopWifi();
            b_powerOff = true;
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
              //#if defined(ACC_MPU6050) || defined(ACC_BMA400)
              digitalWrite(ACC_PWR_CTRL, LOW);
              //#endif
            } else if (data[3] == 0x00) {
              Serial.println("Exit Soft Sleep.");
              digitalWrite(PWR_CTRL, HIGH);
              //#if defined(ACC_MPU6050) || defined(ACC_BMA400)
              digitalWrite(ACC_PWR_CTRL, HIGH);
              //#endif
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
        }
#ifdef BUZZER
        else if (data[1] == 0x1C) {  //buzzer settings
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
        }
#endif
        else if (data[1] == 0x1D) {  //Sample settings
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
        }
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
        else if (data[1] == 0x21) {
          sendBleGyro();
        }
#endif
        else if (data[1] == 0x22) {
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
  pService->start();
  // Start advertising
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();

  Serial.println("Waiting for a client connection to notify...");
}

void disconnectBLE() {
  if (deviceConnected) {
    Serial.println("***No heartbeat for 5 seconds. Disconnecting BLE...***");
    // Only try disconnecting every 5 seconds.
    if (millis() - t_lastDisconnectAttempt < 5000) {
      if (millis() - t_lastDisconnectAttemptNotice > 1000){
        Serial.println("Disconnect attempt too frequent, skipping...");
        t_lastDisconnectAttemptNotice = millis();
      }
      return;
    }
    t_lastDisconnectAttempt = millis();
    pServer->disconnect(connId, 0x13); // must prove connID for proper disconnecting. 0x13 for disconnect from remote device ESP_GAP_BLE_UPDATE_CONN_PARAMS_ERR_REMOTE_DEVICE_DISCONN. 
  }
}

// Build voltage data packet
void buildVoltagePacket(byte data[7]) {
  byte voltageByte1, voltageByte2;
  encodeWeight(f_batteryVoltage, voltageByte1, voltageByte2);

  data[0] = modelByte;
  data[1] = 0x22;  // Voltage type
  data[2] = voltageByte1;
  data[3] = voltageByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);
}

// Send voltage via BLE
void sendBleVoltage() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildVoltagePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

// Build heartbeat packet
void buildHeartBeatPacket(byte data[7]) {
  data[0] = modelByte;
  data[1] = 0x0A;  // Heartbeat type
  data[2] = 0x03;
  data[3] = 0xFF;
  data[4] = 0xFF;
  data[5] = 0x00;
  data[6] = 0x0A;  // Checksum (can also use calculateXOR)
}

// Send heartbeat via BLE
void sendBleHeartBeat() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildHeartBeatPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void buildGyroPacket(byte data[7]) {
  float gyro = gyro_z();
  byte gyroByte1, gyroByte2;
  encodeWeight(gyro, gyroByte1, gyroByte2);

  data[0] = modelByte;
  data[1] = 0x21;  // Gyro type
  data[2] = gyroByte1;
  data[3] = gyroByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);
}

void sendBleGyro() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildGyroPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}
#endif

void buildWeightPacket(byte data[7]) {
  float weight = f_displayedValue;
  byte weightByte1, weightByte2;
  encodeWeight(weight, weightByte1, weightByte2);

  data[0] = modelByte;
  data[1] = 0xCE;  // Weight type
  data[2] = weightByte1;
  data[3] = weightByte2;
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);
}

void sendBleWeight() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastBleWeightNotifyTime < weightBleNotifyInterval) return;
  lastBleWeightNotifyTime = currentMillis;

  byte data[7];
  buildWeightPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

void buildButtonPacket(byte data[7], int buttonNumber, int buttonShortPress) {
  //buttonNumber 1 for button O, 2 for button[]
  //buttonShortPress 1 for short press, 2 for long press
  data[0] = modelByte;
  data[1] = 0xAA;  // Button byte
  data[2] = buttonNumber;
  data[3] = buttonShortPress;
  // 0 for release, 1 for click, 2 for long press
  // Fill the rest with dummy data or real data as needed
  data[4] = 0x00;
  data[5] = 0x00;
  data[6] = calculateXOR(data, 6);
}

void sendBleButton(int buttonNumber, int buttonShortPress) {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildButtonPacket(data, buttonNumber, buttonShortPress);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

// Build PowerOff packet into provided data array
void buildPowerOffPacket(byte data[7], int i_reason) {
  data[0] = modelByte;       // Model byte
  data[1] = 0x2A;            // Command ID for PowerOff

  // Initialize default values
  data[2] = 0x00;
  data[3] = 0x00;
  data[4] = 0x00;
  data[5] = 0x00;

  // Fill reason codes
  switch (i_reason) {
    case 0: data[2] = 0x00; break; // Power off failed: disabled
    case 1: data[2] = 0x10; break; // "O" button double-click
    case 2: data[2] = 0x11; break; // "[]" button double-click
    case 3: data[3] = 0x20; break; // Low battery
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
    case 4: data[2] = 0x30; break; // Power off from gyro
#endif
    default: data[2] = 0x00; break; // Invalid reason
  }

  // XOR checksum
  data[6] = calculateXOR(data, 6);
}

void sendBlePowerOff(int i_reason) {
  // Check BLE enabled, device connected, characteristic exists
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;

  byte data[7];
  buildPowerOffPacket(data, i_reason);

  // Send BLE notification
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}


/*
Automatic firmware version extraction from LINE1 (FW: x.y.z)
BCD encoding for firmware version
Weight encoding using encodeWeight()
Charging detection (USB_DET or BATTERY_CHARGING)
Battery byte: 0xFF if charging, 0x64 (100%) otherwise
No XOR checksum
*/

// -----------------------------
// Build common LED response packet
// -----------------------------
void buildLedResponsePacket(byte data[7]) {
  int major = 0, minor = 0, patch = 0;

  // 1. Extract firmware version
  if (sscanf(LINE1, "FW: %d.%d.%d", &major, &minor, &patch) != 3) {
    major = 0;
    minor = 0;
    patch = 0;
  }

  // Convert to BCD format
  byte verHigh = (byte)(((major / 10) << 4) | (major % 10));
  byte verLow  = (byte)((minor << 4) | patch);

  // 2. Get current weight
  float weight = f_displayedValue;
  byte weightByte1, weightByte2;
  encodeWeight(weight, weightByte1, weightByte2);

  // 3. Detect charging status
  bool b_is_charging = false;
#if defined(USB_DET)
  b_is_charging = (digitalRead(USB_DET) == LOW);
#else
  b_is_charging = (digitalRead(BATTERY_CHARGING) == LOW);
#endif

  // 4. Compute battery level
  byte batteryByte;
  if (b_is_charging) {
    batteryByte = 0xFF; // Charging
  } else {
    float perc = (f_batteryVoltage - showEmptyBatteryBelowVoltage) /
                 (showFullBatteryAboveVoltage - showEmptyBatteryBelowVoltage) * 100.0f;
    if (perc < 0) perc = 0;
    if (perc > 100) perc = 100;
    batteryByte = (byte)perc;
  }

  // 5. Fill packet
  data[0] = 0x03;         // Header
  data[1] = 0x0A;         // Type (LED response)
  data[2] = weightByte1;  // Weight high
  data[3] = weightByte2;  // Weight low
  data[4] = batteryByte;  // Battery / charging indicator
  data[5] = verHigh;      // Firmware version high
  data[6] = verLow;       // Firmware version low
}

void sendBleLedResponse() {
  // Check BLE enabled, device connected, characteristic exists
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;

  byte data[7];
  buildLedResponsePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

#endif