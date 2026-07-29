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
const unsigned long BLE_STATUS_RESPONSE_TIMEOUT = 2000;
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
void queueBleStatusResponse();
// Defined in usbcomm.h, included after ble.h in hds.ino
void buildAdsDebugPacket(byte data[41]);
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif
volatile uint16_t connId = 0xFFFF; // not set to 0 because 0 could be a valid client id.

void resetBleFff4StateLocked(uint16_t subscriptionHandle) {
  bleFff4SubscriptionHandle = subscriptionHandle;
  bleStatusResponsePending = false;
  bleStatusRequestAt = 0;
  bleNotifyFailureLogged = false;
}

void setBleFff4Connection(uint16_t connectionHandle, uint16_t subscriptionHandle) {
  portENTER_CRITICAL(&bleFff4Mux);
  bleFff4ConnectionGeneration = bleFff4ConnectionGeneration + 1;
  connId = connectionHandle;
  resetBleFff4StateLocked(subscriptionHandle);
  portEXIT_CRITICAL(&bleFff4Mux);
}

bool clearBleFff4Connection(uint16_t connectionHandle) {
  portENTER_CRITICAL(&bleFff4Mux);
  const bool isCurrent = connectionHandle == connId;
  if (isCurrent) {
    bleFff4ConnectionGeneration = bleFff4ConnectionGeneration + 1;
    connId = 0xFFFF;
    resetBleFff4StateLocked(0xFFFF);
  }
  portEXIT_CRITICAL(&bleFff4Mux);
  return isCurrent;
}


// This callback will be invoked when a device connects or disconnects.
//
// The connection-aware (conn_handle) overloads are required: a transient
// central reconnect can deliver the disconnect event for an old handle
// *after* the new connection's onConnect. Tracking conn_handle lets us
// ignore those stale disconnects instead of clearing state on a live link.
class MyServerCallbacks : public BLEServerCallbacks {
#if defined(CONFIG_NIMBLE_ENABLED)
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    setBleFff4Connection(desc->conn_handle, 0xFFFF);
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
    if (!clearBleFff4Connection(desc->conn_handle)) {
      //stale disconnect for an old handle — a newer connection is still live
      Serial.print("Ignoring stale BLE disconnect for conn_handle: ");
      Serial.println(desc->conn_handle);
      return;
    }
    deviceConnected = false;
    bleState = DISCONNECTED;
#ifdef BUZZER
    b_beep = storageGetInt(KEY_BEEP, 1);
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
    const uint16_t connectionHandle = pServer->getConnId();
    setBleFff4Connection(connectionHandle, connectionHandle);
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
    setBleFff4Connection(0xFFFF, 0xFFFF);
    deviceConnected = false;
    bleState = DISCONNECTED;
#ifdef BUZZER
    b_beep = storageGetInt(KEY_BEEP, 1);
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
struct BleDecentCommandSink {
  const char *transportName() {
    return "BLE";
  }

  void requestTare() {
    requestRemoteTare();
  }

  void displayOff() {
    b_u8g2Sleep = true;
    remoteReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);
    queueBleStatusResponse();
  }

  void displayOn() {
    b_u8g2Sleep = false;
    remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
    queueBleStatusResponse();
  }

  void powerOff() {
    remoteQueuePending(WSP_POWER_OFF);
  }

  void lowPowerOn() {
    b_websocketLowPowerEnabled = true;
    remoteReplacePending(WSP_LOWPWR_ON, WSP_LOWPWR_OFF);
  }

  void lowPowerOff() {
    b_websocketLowPowerEnabled = false;
    remoteReplacePending(WSP_LOWPWR_OFF, WSP_LOWPWR_ON);
  }

  void softSleepOn() {
    b_softSleep = true;
    b_u8g2Sleep = true;
    remoteReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
  }

  void softSleepOff() {
    bool wasSoftSleep = b_softSleep;
    b_softSleep = false;
    b_u8g2Sleep = false;
    if (wasSoftSleep) {
      remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON);
    } else {
      remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
    }
  }

  void timerStart() {
    remoteReplacePending(WSP_TIMER_START, WSP_TIMER_STOP | WSP_TIMER_ZERO);
  }

  void timerStop() {
    remoteReplacePending(WSP_TIMER_STOP, WSP_TIMER_START | WSP_TIMER_ZERO);
  }

  void timerZero() {
    remoteReplacePending(WSP_TIMER_ZERO, WSP_TIMER_START | WSP_TIMER_STOP);
  }

  void wifiUpdate() {
    Serial.println("Start WiFi OTA queued.");
    remoteQueuePending(WSP_WIFI_UPDATE);
  }

#ifdef BUZZER
  void buzzerOff() {
    b_beep = false;
  }

  void buzzerOn() {
    b_beep = true;
  }

  void buzzerBeep() {
    buzzer.beep(1, BUZZER_DURATION);
  }
#endif

  void setSamplesInUse(uint8_t samplesInUse) {
    remoteQueueSamplesInUse(samplesInUse);
    Serial.print("Samples in use queued: ");
    Serial.println(samplesInUse);
  }

  void reset() {
    Serial.println("Reset queued.");
    remoteQueuePending(WSP_RESET);
  }

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
  void sendGyro() {
    Serial.println("BLE gyro response queued.");
    remoteQueuePending(WSP_BLE_GYRO);
  }
#endif

  void sendVoltage() {
    sendBleVoltage();
  }

  void adsDebug(uint8_t mode) {
    if (mode == 0x00) {
      Serial.println("BLE ADS debug: OFF");
      bleDebugMode = DEBUG_OFF;
    } else if (mode == 0x01) {
      Serial.println("BLE ADS debug: CONTINUOUS");
      bleDebugMode = DEBUG_CONTINUOUS;
    } else if (mode == 0x02) {
      Serial.println("BLE ADS debug: SINGLE");
      bleDebugMode = DEBUG_SINGLE;
    }
  }

  bool supportsAdsReset() {
    return false;
  }

  void adsReset(uint8_t mode) {
  }
};

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
        BleDecentCommandSink sink;
        handleDecentBinaryCommand(sink, data, len);
      }
    }
  }
};

class Fff4Callbacks : public BLECharacteristicCallbacks {
#if defined(CONFIG_NIMBLE_ENABLED)
  void onSubscribe(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc, uint16_t subValue) {
    if (desc == nullptr) return;
    const uint16_t nextHandle = subValue == 0 ? 0xFFFF : desc->conn_handle;
    portENTER_CRITICAL(&bleFff4Mux);
    const bool changed = desc->conn_handle == connId && bleFff4SubscriptionHandle != nextHandle;
    if (changed) bleFff4SubscriptionHandle = nextHandle;
    portEXIT_CRITICAL(&bleFff4Mux);
    if (!changed) return;
    Serial.printf("FFF4 notifications %s for connId: %u\n", subValue == 0 ? "disabled" : "enabled",
                  static_cast<unsigned int>(desc->conn_handle));
  }
#endif

  void onStatus(BLECharacteristic *pCharacteristic, BLECharacteristicCallbacks::Status status, uint32_t code) {
    if (status == SUCCESS_NOTIFY || status == SUCCESS_INDICATE) return;
    portENTER_CRITICAL(&bleFff4Mux);
    const bool shouldLog = !bleNotifyFailureLogged && connId != 0xFFFF && bleFff4SubscriptionHandle == connId;
    const uint16_t currentConnId = connId;
    if (shouldLog) bleNotifyFailureLogged = true;
    portEXIT_CRITICAL(&bleFff4Mux);
    if (!shouldLog) return;
    Serial.printf("FFF4 notification failure for connId: %u, status: %u, code: %lu\n",
                  static_cast<unsigned int>(currentConnId), static_cast<unsigned int>(status), static_cast<unsigned long>(code));
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
  pReadCharacteristic->setCallbacks(new Fff4Callbacks());
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

static bool bleHasLiveClient() {
#if defined(CONFIG_NIMBLE_ENABLED)
  return pServer != nullptr && pServer->getConnectedCount() > 0;
#else
  return deviceConnected;
#endif
}

static bool bleCanNotifyCurrent() {
  portENTER_CRITICAL(&bleFff4Mux);
  const bool subscribed = connId != 0xFFFF && bleFff4SubscriptionHandle == connId;
  portEXIT_CRITICAL(&bleFff4Mux);
  return b_ble_enabled
    && pReadCharacteristic != nullptr
    && bleHasLiveClient()
    && subscribed;
}

void queueBleStatusResponse() {
  const unsigned long now = millis();
  portENTER_CRITICAL(&bleFff4Mux);
  bleStatusRequestAt = now;
  bleStatusResponsePending = true;
  portEXIT_CRITICAL(&bleFff4Mux);
}

void processBleStatusResponse() {
  const unsigned long now = millis();
  bool sendStatus = false;
  bool disconnectCurrent = false;
  uint16_t currentConnId = 0xFFFF;
  uint32_t connectionGeneration = 0;
  portENTER_CRITICAL(&bleFff4Mux);
  if (bleStatusResponsePending) {
    currentConnId = connId;
    connectionGeneration = bleFff4ConnectionGeneration;
    if (currentConnId != 0xFFFF && bleFff4SubscriptionHandle == currentConnId) {
      bleStatusResponsePending = false;
      sendStatus = true;
    } else if (now - bleStatusRequestAt >= BLE_STATUS_RESPONSE_TIMEOUT) {
      bleStatusResponsePending = false;
      disconnectCurrent = currentConnId != 0xFFFF;
    }
  }
  portEXIT_CRITICAL(&bleFff4Mux);
  if (sendStatus) {
    sendBleLedResponse();
    return;
  }
  if (!disconnectCurrent || pServer == nullptr || !bleHasLiveClient()) return;
  portENTER_CRITICAL(&bleFff4Mux);
  if (connId != currentConnId || bleFff4ConnectionGeneration != connectionGeneration || bleStatusResponsePending) {
    disconnectCurrent = false;
  } else if (bleFff4SubscriptionHandle == currentConnId) {
    disconnectCurrent = false;
    sendStatus = true;
  }
  portEXIT_CRITICAL(&bleFff4Mux);
  if (sendStatus) {
    sendBleLedResponse();
    return;
  }
  if (!disconnectCurrent) return;
  Serial.print("FFF4 subscription timeout, disconnecting connId: ");
  Serial.println(currentConnId);
  pServer->disconnect(currentConnId, 0x13);
}

// Send voltage via BLE
void sendBleVoltage() {
  if (!bleCanNotifyCurrent()) return;
  byte data[7];
  buildVoltagePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

// Send heartbeat via BLE
void sendBleHeartBeat() {
  if (!bleCanNotifyCurrent()) return;
  byte data[7];
  buildHeartBeatPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro() {
  if (!bleCanNotifyCurrent()) return;
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
  if (!bleCanNotifyCurrent()) return;
  byte data[7];
  buildWeightPacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

void sendBleButton(int buttonNumber, int buttonShortPress) {
  if (!bleCanNotifyCurrent()) return;
  byte data[7];
  buildButtonPacket(data, buttonNumber, buttonShortPress);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

void sendBlePowerOff(int i_reason) {
  // Check BLE enabled, device connected, characteristic exists
  if (!bleCanNotifyCurrent()) return;

  byte data[7];
  buildPowerOffPacket(data, i_reason);

  // Send BLE notification
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}


void sendBleLedResponse() {
  // Check BLE enabled, device connected, characteristic exists
  if (!bleCanNotifyCurrent()) return;

  byte data[7];
  buildLedResponsePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

// Send 41-byte ADS1232 debug packet via BLE notify on fff4.
// In SINGLE mode, auto-clears bleDebugMode to OFF after sending.
void sendAdsDebugInfoBLE() {
  if (!bleCanNotifyCurrent()) return;
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
