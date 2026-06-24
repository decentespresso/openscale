#ifndef BLE_H
#define BLE_H
#include "declare.h"
#include "decent_protocol.h"
#include <math.h>


// #include <BluetoothSerial.h>
// BluetoothSerial SerialBT;
enum BleState {
  DISCONNECTED,
  CONNECTED
};
volatile BleState bleState = DISCONNECTED;
const unsigned long HEARTBEAT_TIMEOUT = 5000;  // 5 seconds
unsigned long t_lastDisconnectAttempt = 0;
unsigned long t_lastDisconnectAttemptNotice = 0;

// ADS1232 debug streaming mode over BLE (cmd 0x25)
enum DebugMode {
  DEBUG_OFF = 0,
  DEBUG_SINGLE = 1,
  DEBUG_CONTINUOUS = 2
};
volatile DebugMode bleDebugMode = DEBUG_OFF;
unsigned long t_lastBleDebugNotify = 0;
const unsigned long BLE_DEBUG_MIN_INTERVAL = 100;  // ~10 Hz cap for continuous mode

//functions
void sendBleVoltage();
void sendBleLedResponse();
void sendAdsDebugInfoBLE();
// Defined in usbcomm.h, included after ble.h in hds.ino
void buildAdsDebugPacket(byte data[41]);
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif
volatile uint16_t connId = 0xFFFF; // not set to 0 because 0 could be a valid client id.


// This callback will be invoked when a device connects or disconnects.
//
// The connection-aware (conn_handle) overloads are required: a transient
// central reconnect can deliver the disconnect event for an old handle
// *after* the new connection's onConnect. Tracking conn_handle lets us
// ignore those stale disconnects instead of clearing state on a live link.
class MyServerCallbacks : public BLEServerCallbacks {
#if defined(CONFIG_NIMBLE_ENABLED)
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    connId = desc->conn_handle;
    t_firstConnect = millis();
    t_heartBeat = millis();
    bleState = CONNECTED;
    deviceConnected = true;
#ifdef BUZZER
    b_beep = false;  //disable buzzer once when connected, and wait for ble command to enable it
#endif
    pAdvertising->stop();  //single-client device — don't accept a second connection
    Serial.print("Device connected, connId: ");
    Serial.println(connId);
  }

  void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    if (desc->conn_handle != connId) {
      //stale disconnect for an old handle — a newer connection is still live
      Serial.print("Ignoring stale BLE disconnect for conn_handle: ");
      Serial.println(desc->conn_handle);
      return;
    }
    connId = 0xFFFF;  //not set to 0 because 0 could be a valid client id
    deviceConnected = false;
    bleState = DISCONNECTED;
#ifdef BUZZER
    EEPROM.get(i_addr_beep, b_beep);  //read buzzer state after disconnect
#endif
    b_u8g2Sleep = false;
    remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
    Serial.print("Device disconnected (connId: ");
    Serial.print(desc->conn_handle);
    Serial.println("), restarting advertising...");
    delay(100);  // advertise after a short delay
    pAdvertising->start();
  }
#else
  // Bluedroid fallback (classic ESP32). Not used for esp32s3 builds.
  void onConnect(BLEServer *pServer) {
    connId = pServer->getConnId();
    t_firstConnect = millis();
    t_heartBeat = millis();
    bleState = CONNECTED;
    deviceConnected = true;
#ifdef BUZZER
    b_beep = false;
#endif
    pAdvertising->stop();
    Serial.print("Device connected, connId: ");
    Serial.println(connId);
  }

  void onDisconnect(BLEServer *pServer) {
    connId = 0xFFFF;
    deviceConnected = false;
    bleState = DISCONNECTED;
#ifdef BUZZER
    EEPROM.get(i_addr_beep, b_beep);
#endif
    b_u8g2Sleep = false;
    remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
    Serial.println("Device disconnected, restarting advertising...");
    delay(100);
    pAdvertising->start();
  }
#endif
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

      if (data == nullptr || len <= 0) {
        Serial.println("Ignoring empty BLE write.");
        return;
      }

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
        if (!decentRequireLength("BLE", len, 2, "message header")) {
          return;
        }
        //check if it's a decent scale message
        if (data[1] == 0x0F) {
          if (!decentRequireLength("BLE", len, 7, "tare")) {
            return;
          }
          //taring
          if (decentValidateChecksum(data, len)) {
            Serial.println("Valid checksum for tare operation. Taring");
          } else {
            Serial.println("Invalid checksum for tare operation.");
            return;
          }
          requestRemoteTare();
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
          if (!decentRequireLength("BLE", len, 3, "LED/power")) {
            return;
          }
          if (data[2] == 0x00) {
            Serial.println("LED off detected. Turn off OLED.");
            b_u8g2Sleep = true;
            remoteReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);
            sendBleLedResponse();//include weight voltage version
          } else if (data[2] == 0x01) {
            Serial.println("LED on detected. Turn on OLED.");
            b_u8g2Sleep = false;
            remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
            sendBleLedResponse();//including weight voltage version
            if (!decentRequireLength("BLE", len, 6, "LED on")) {
              return;
            }
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
            remoteQueuePending(WSP_POWER_OFF);
          } else if (data[2] == 0x03) {
            if (!decentRequireLength("BLE", len, 4, "low power")) {
              return;
            }
            if (data[3] == 0x01) {
              Serial.println("Start Low Power Mode.");
              b_websocketLowPowerEnabled = true;
              remoteReplacePending(WSP_LOWPWR_ON, WSP_LOWPWR_OFF);
            } else if (data[3] == 0x00) {
              Serial.println("Exit low power mode.");
              b_websocketLowPowerEnabled = false;
              remoteReplacePending(WSP_LOWPWR_OFF, WSP_LOWPWR_ON);
            } else if (data[3] == 0xFF) {
              if (!decentRequireLength("BLE", len, 7, "heartbeat")) {
                return;
              }
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
            if (!decentRequireLength("BLE", len, 4, "soft sleep")) {
              return;
            }
            if (data[3] == 0x01) {
              Serial.println("Start Soft Sleep.");
              b_softSleep = true;
              b_u8g2Sleep = true;
              remoteReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
            } else if (data[3] == 0x00) {
              Serial.println("Exit Soft Sleep.");
              b_softSleep = false;
              b_u8g2Sleep = false;
              remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);
            }
          }
        } else if (data[1] == 0x0B) {
          if (!decentRequireLength("BLE", len, 3, "timer")) {
            return;
          }
          if (data[2] == 0x03) {
            Serial.println("Timer start detected.");
            remoteReplacePending(WSP_TIMER_START, WSP_TIMER_STOP | WSP_TIMER_ZERO);
          } else if (data[2] == 0x00) {
            Serial.println("Timer stop detected.");
            remoteReplacePending(WSP_TIMER_STOP, WSP_TIMER_START | WSP_TIMER_ZERO);
          } else if (data[2] == 0x02) {
            Serial.println("Timer zero detected.");
            remoteReplacePending(WSP_TIMER_ZERO, WSP_TIMER_START | WSP_TIMER_STOP);
          }
        } else if (data[1] == 0x1A) {
          if (!decentRequireLength("BLE", len, 3, "calibration")) {
            return;
          }
          if (data[2] == 0x00) {
            Serial.println("Manual Calibration via BLE");
            b_menu = false;
            i_cal_weight = 0;
            i_button_cal_status = 1;
            i_calibration = 0;
            b_calibration = true;
          } else if (data[2] == 0x01) {
            Serial.println("Smart Calibration via BLE");
            b_menu = false;
            i_cal_weight = 0;
            i_button_cal_status = 1;
            i_calibration = 1;
            b_calibration = true;
          }
        } else if (data[1] == 0x1B) {
          Serial.println("Start WiFi OTA queued.");
          remoteQueuePending(WSP_WIFI_UPDATE);
        }
#ifdef BUZZER
        else if (data[1] == 0x1C) {  //buzzer settings
          if (!decentRequireLength("BLE", len, 3, "buzzer")) {
            return;
          }
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
          if (!decentRequireLength("BLE", len, 3, "sample settings")) {
            return;
          }
          if (data[2] == 0x00) {
            remoteQueueSamplesInUse(1);
            Serial.println("Samples in use queued: 1");
          } else if (data[2] == 0x01) {
            remoteQueueSamplesInUse(2);
            Serial.println("Samples in use queued: 2");
          } else if (data[2] == 0x03) {
            remoteQueueSamplesInUse(4);
            Serial.println("Samples in use queued: 4");
          }
        } else if (data[1] == 0x1E) {
          if (!decentRequireLength("BLE", len, 4, "menu/about/debug")) {
            return;
          }
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
          Serial.println("Reset queued.");
          remoteQueuePending(WSP_RESET);
        } else if (data[1] == 0x20) {
          if (!decentRequireLength("BLE", len, 3, "USB weight")) {
            return;
          }
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
          Serial.println("BLE gyro response queued.");
          remoteQueuePending(WSP_BLE_GYRO);
        }
#endif
        else if (data[1] == 0x22) {
          sendBleVoltage();
        }
        else if (data[1] == 0x25) {
          if (!decentRequireLength("BLE", len, 3, "ADS debug BLE")) {
            return;
          }
          // 0x25 ADS1232 debug streaming over BLE
          if (data[2] == 0x00) {
            Serial.println("BLE ADS debug: OFF");
            bleDebugMode = DEBUG_OFF;
          } else if (data[2] == 0x01) {
            Serial.println("BLE ADS debug: CONTINUOUS");
            bleDebugMode = DEBUG_CONTINUOUS;
          } else if (data[2] == 0x02) {
            Serial.println("BLE ADS debug: SINGLE");
            bleDebugMode = DEBUG_SINGLE;
          }
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

// Gracefully tear down BLE before power-off or restart. Sends an LL
// terminate so the peer sees a clean disconnect instead of waiting out
// the supervision timeout, then stops advertising and releases the
// stack. Main-task use only — it blocks ~300 ms when a client is
// connected. No-ops if BLE was never initialized.
void bleShutdown() {
  if (pServer == nullptr) return;
  if (deviceConnected && connId != 0xFFFF) {
    pServer->disconnect(connId, 0x13);
    delay(300);  // let the LL terminate transmit and onDisconnect run
  }
  if (pAdvertising != nullptr) {
    pAdvertising->stop();
  }
  BLEDevice::deinit(true);
}

// Send voltage via BLE
void sendBleVoltage() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildVoltagePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
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
void sendBleGyro() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildGyroPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}
#endif

// Rate-gating is handled centrally by the unified weight-output tick in the
// main loop (which honors weightBleNotifyInterval, fixed at 100 ms / 10 Hz for
// BLE). This just emits one notification when called; the active-state check
// stays so we never touch the characteristic without a live connection.
void sendBleWeight() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildWeightPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

void sendBleButton(int buttonNumber, int buttonShortPress) {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  byte data[7];
  buildButtonPacket(data, buttonNumber, buttonShortPress);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
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


void sendBleLedResponse() {
  // Check BLE enabled, device connected, characteristic exists
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;

  byte data[7];
  buildLedResponsePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

// Send 41-byte ADS1232 debug packet via BLE notify on fff4.
// In SINGLE mode, auto-clears bleDebugMode to OFF after sending.
void sendAdsDebugInfoBLE() {
  if (!(b_ble_enabled && deviceConnected && pReadCharacteristic)) return;
  if (bleDebugMode == DEBUG_OFF) return;

  byte data[41];
  buildAdsDebugPacket(data);
  pReadCharacteristic->setValue(data, 41);
  pReadCharacteristic->notify();

  if (bleDebugMode == DEBUG_SINGLE) {
    bleDebugMode = DEBUG_OFF;
  }
}

#endif
