#ifndef BLE_H
#define BLE_H
#include "declare.h"
#include "decent_protocol.h"
#include <math.h>


enum BleState {
  DISCONNECTED,
  CONNECTED
};
volatile BleState bleState = DISCONNECTED;
const unsigned long HEARTBEAT_TIMEOUT = 5000;
const unsigned long HEARTBEAT_DISCONNECT_RETRY_INTERVAL = 10000;
const unsigned long BLE_STATUS_RESPONSE_TIMEOUT = 2000;
unsigned long t_lastDisconnectAttempt = 0;
unsigned long t_lastDisconnectAttemptNotice = 0;

enum DebugMode {
  DEBUG_OFF = 0,
  DEBUG_SINGLE = 1,
  DEBUG_CONTINUOUS = 2
};
volatile DebugMode bleDebugMode = DEBUG_OFF;
unsigned long t_lastBleDebugNotify = 0;
const unsigned long BLE_DEBUG_MIN_INTERVAL = 100;

void sendBleVoltage();
void sendBleLedResponse();
void sendAdsDebugInfoBLE();
void queueBleStatusResponse();
void queueBleVoltageResponse();
void processBleVoltageResponse();
static bool bleHasLiveClient();
void buildAdsDebugPacket(byte data[41]);
#if defined(ACC_MPU6050) || defined(ACC_BMA400)
void sendBleGyro();
#endif
volatile uint16_t connId = 0xFFFF;

void resetBleFff4StateLocked(uint16_t subscriptionHandle) {
  bleFff4SubscriptionHandle = subscriptionHandle;
  bleStatusResponsesPending = 0;
  bleVoltageResponsesPending = 0;
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

void restoreDisplayAfterBleDisconnect() {
  if (b_softSleep) return;
  b_u8g2Sleep = false;
  remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
}


class MyServerCallbacks : public BLEServerCallbacks {
#if defined(CONFIG_NIMBLE_ENABLED)
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    setBleFff4Connection(desc->conn_handle, 0xFFFF);
    t_firstConnect = millis();
    t_heartBeat = millis();
    bleState = CONNECTED;
    deviceConnected = true;
#if HDS_ENABLE_ENERGY_MENU
    recordEnergyActivity();
#endif
#ifdef BUZZER
    b_beep = false;
#endif
    pAdvertising->stop();
    Serial.print("Device connected, connId: ");
    Serial.println(connId);
  }

  void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) {
    if (!clearBleFff4Connection(desc->conn_handle)) {
      Serial.print("Ignoring stale BLE disconnect for conn_handle: ");
      Serial.println(desc->conn_handle);
      return;
    }
    deviceConnected = false;
    bleState = DISCONNECTED;
#ifdef BUZZER
    b_beep = storageGetInt(KEY_BEEP, 1);
#endif
    restoreDisplayAfterBleDisconnect();
    Serial.print("Device disconnected (connId: ");
    Serial.print(desc->conn_handle);
    Serial.println("), restarting advertising...");
    delay(100);
    pAdvertising->start();
  }
#else
  void onConnect(BLEServer *pServer) {
    const uint16_t connectionHandle = pServer->getConnId();
    setBleFff4Connection(connectionHandle, connectionHandle);
    t_firstConnect = millis();
    t_heartBeat = millis();
    bleState = CONNECTED;
    deviceConnected = true;
#if HDS_ENABLE_ENERGY_MENU
    recordEnergyActivity();
#endif
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
    restoreDisplayAfterBleDisconnect();
    Serial.println("Device disconnected, restarting advertising...");
    delay(100);
    pAdvertising->start();
  }
#endif
};
struct BleDecentCommandSink {
  const char *transportName() {
    return "BLE";
  }

  void requestTare() {
    requestRemoteTare();
  }

  void displayOff() {
#if HDS_ENABLE_ENERGY_MENU
    requestEnergyDisplay(false);
#else
    b_u8g2Sleep = true;
#endif
    remoteReplacePending(WSP_DISPLAY_OFF, WSP_DISPLAY_ON);
    queueBleStatusResponse();
  }

  void displayOn() {
#if HDS_ENABLE_ENERGY_MENU
    requestEnergyDisplay(true);
#else
    b_u8g2Sleep = false;
#endif
    remoteReplacePending(WSP_DISPLAY_ON, WSP_DISPLAY_OFF);
    queueBleStatusResponse();
  }

  void powerOff() {
    remoteQueuePending(WSP_POWER_OFF);
  }

  void lowPowerOn() {
#if HDS_ENABLE_ENERGY_MENU
    requestEnergyLowPower(true);
#else
    b_websocketLowPowerEnabled = true;
#endif
    remoteReplacePending(WSP_LOWPWR_ON, WSP_LOWPWR_OFF);
  }

  void lowPowerOff() {
#if HDS_ENABLE_ENERGY_MENU
    requestEnergyLowPower(false);
#else
    b_websocketLowPowerEnabled = false;
#endif
    remoteReplacePending(WSP_LOWPWR_OFF, WSP_LOWPWR_ON);
  }

  void softSleepOn() {
    remoteReplacePending(WSP_SLEEP_ON, WSP_SLEEP_OFF);
  }

  void softSleepOff() {
    remoteReplacePending(WSP_SLEEP_OFF, WSP_SLEEP_ON | WSP_DISPLAY_OFF);
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
#if HDS_FEATURE_PULL_OTA
    Serial.println("Start WiFi OTA queued.");
    remoteQueuePending(WSP_WIFI_UPDATE);
#else
    Serial.println("WiFi OTA unavailable.");
#endif
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
    queueBleVoltageResponse();
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
    Serial.print("Timer");
    Serial.print(millis());
    Serial.print(" onWrite counter:");
    Serial.print(i_onWrite_counter++);
    Serial.print(" ");

    if (pWriteCharacteristic != nullptr) {
      size_t len = pWriteCharacteristic->getLength();
      uint8_t *data = (uint8_t *)pWriteCharacteristic->getData();

      if (data == nullptr || len <= 0) {
        Serial.println("Ignoring empty BLE write.");
        return;
      }

      Serial.print("Received HEX: ");
      for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) {
          Serial.print("0");
        }
        Serial.print(data[i], HEX);
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
  BLEDevice::init("Decent Scale");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
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
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();

  Serial.println("Waiting for a client connection to notify...");
}

void disconnectBLE() {
  if (!bleHasLiveClient() || pServer == nullptr || connId == 0xFFFF) return;
  const unsigned long now = millis();
  if (now - t_lastDisconnectAttempt < HEARTBEAT_TIMEOUT) {
    if (now - t_lastDisconnectAttemptNotice > 1000){
      Serial.println("Disconnect attempt too frequent, skipping...");
      t_lastDisconnectAttemptNotice = now;
    }
    return;
  }
  Serial.println("***No heartbeat for 5 seconds. Disconnecting BLE...***");
  t_lastDisconnectAttempt = now;
  pServer->disconnect(connId, 0x13);
}

void bleShutdown() {
  if (pServer == nullptr) return;
  if (bleHasLiveClient() && connId != 0xFFFF) {
    pServer->disconnect(connId, 0x13);
    delay(300);
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

void queueBleVoltageResponse() {
  portENTER_CRITICAL(&bleFff4Mux);
  if (bleVoltageResponsesPending != UINT16_MAX) bleVoltageResponsesPending = bleVoltageResponsesPending + 1;
  portEXIT_CRITICAL(&bleFff4Mux);
}

void processBleVoltageResponse() {
  bool sendVoltage = false;
  portENTER_CRITICAL(&bleFff4Mux);
  if (bleVoltageResponsesPending > 0) {
    bleVoltageResponsesPending = bleVoltageResponsesPending - 1;
    sendVoltage = true;
  }
  portEXIT_CRITICAL(&bleFff4Mux);
  if (sendVoltage) sendBleVoltage();
}

void queueBleStatusResponse() {
  const unsigned long now = millis();
  portENTER_CRITICAL(&bleFff4Mux);
  bleStatusRequestAt = now;
  if (bleStatusResponsesPending != UINT16_MAX) bleStatusResponsesPending = bleStatusResponsesPending + 1;
  portEXIT_CRITICAL(&bleFff4Mux);
}

void processBleStatusResponse() {
  if (bleStatusResponsesPending == 0) return;
  const unsigned long now = millis();
  bool sendStatus = false;
  bool disconnectCurrent = false;
  uint16_t currentConnId = 0xFFFF;
  uint32_t connectionGeneration = 0;
  portENTER_CRITICAL(&bleFff4Mux);
  if (bleStatusResponsesPending > 0) {
    currentConnId = connId;
    connectionGeneration = bleFff4ConnectionGeneration;
    if (currentConnId != 0xFFFF && bleFff4SubscriptionHandle == currentConnId) {
      bleStatusResponsesPending = bleStatusResponsesPending - 1;
      sendStatus = true;
    } else if (now - bleStatusRequestAt >= BLE_STATUS_RESPONSE_TIMEOUT) {
      bleStatusResponsesPending = 0;
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
  if (connId != currentConnId || bleFff4ConnectionGeneration != connectionGeneration || bleStatusResponsesPending > 0) {
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

void sendBleVoltage() {
  if (!bleCanNotifyCurrent()) return;
  byte data[7];
  buildVoltagePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

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
  if (!bleCanNotifyCurrent()) return;

  byte data[7];
  buildPowerOffPacket(data, i_reason);

  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}


void sendBleLedResponse() {
  if (!bleCanNotifyCurrent()) return;

  byte data[7];
  buildLedResponsePacket(data);
  pReadCharacteristic->setValue(data, 7);
  pReadCharacteristic->notify();
}

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
