#ifndef PRESSENSOR_BLE_H
#define PRESSENSOR_BLE_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>
#if defined(CONFIG_NIMBLE_ENABLED)
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#endif

#define PRESSENSOR_SERVICE_UUID "873ae82a-4c5a-4342-b539-9d900bf7ebd0"
#define PRESSENSOR_PRESSURE_CHAR_UUID "873ae82b-4c5a-4342-b539-9d900bf7ebd0"
#define PRESSENSOR_ZERO_CHAR_UUID "873ae82c-4c5a-4342-b539-9d900bf7ebd0"
#define PRESSENSOR_NAME_MARKER "PRS"

#define PRESSENSOR_SCAN_SECONDS 5
#define PRESSENSOR_RESCAN_DELAY_MS 3000
#define PRESSENSOR_SETTLE_MS 1200
#define PRESSENSOR_PHASE_TIMEOUT_MS 6000
#define PRESSENSOR_STALE_MS 4000
#define PRESSENSOR_SCAN_LIST_MAX 6

enum PressensorLinkState {
  PRESSENSOR_LINK_OFF,
  PRESSENSOR_LINK_SCAN_WAIT,
  PRESSENSOR_LINK_SCANNING,
  PRESSENSOR_LINK_CONNECTING,
  PRESSENSOR_LINK_SETTLING,
  PRESSENSOR_LINK_DISC_SVC,
  PRESSENSOR_LINK_DISC_CHR,
  PRESSENSOR_LINK_DISC_DSC,
  PRESSENSOR_LINK_SUBSCRIBING,
  PRESSENSOR_LINK_STREAMING
};

struct PressensorScanEntry {
  char mac[18] = { 0 };
  char name[24] = { 0 };
  int rssi = -127;
};

struct PressensorLink {
  PressensorLinkState state = PRESSENSOR_LINK_OFF;
  PressensorScanEntry scanList[PRESSENSOR_SCAN_LIST_MAX];
  uint8_t scanListCount = 0;
  char targetMac[18] = { 0 };
  bool matchAnyDevice = false;
  uint32_t nextScanAt = 0;
  uint32_t phaseStartedAt = 0;
  ble_addr_t peerAddr = {};
  volatile bool matchFound = false;
  volatile bool scanEnded = false;
  volatile bool dropped = false;
  volatile bool phaseDone = false;
  volatile bool phaseFailed = false;
  volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
  volatile uint16_t svcStartHandle = 0;
  volatile uint16_t svcEndHandle = 0;
  volatile uint16_t pressureValHandle = 0;
  volatile uint16_t zeroValHandle = 0;
  volatile uint16_t pressureCccdHandle = 0;
};

PressensorLink pressensorLink;
portMUX_TYPE pressensorMux = portMUX_INITIALIZER_UNLOCKED;
volatile float pressensorBarShared = 0.0f;
volatile uint32_t pressensorNotifyAtShared = 0;

static const ble_uuid128_t pressensorSvcUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2a, 0xe8, 0x3a, 0x87);
static const ble_uuid128_t pressensorPressureUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2b, 0xe8, 0x3a, 0x87);
static const ble_uuid128_t pressensorZeroUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2c, 0xe8, 0x3a, 0x87);

static inline float pressensorParseBar(const uint8_t *data, size_t length) {
  if (data == nullptr || length < 2) {
    return NAN;
  }
  int32_t raw = ((int32_t)data[0] << 8) + data[1];
  if (raw >= 0x8000) {
    raw = -1 * (0xFFFF - raw + 1);
  }
  return raw / 1000.0f;
}

static inline void pressensorStoreBar(float bar) {
  portENTER_CRITICAL(&pressensorMux);
  pressensorBarShared = bar;
  pressensorNotifyAtShared = millis();
  portEXIT_CRITICAL(&pressensorMux);
}

static inline float pressensorReadBar(uint32_t *notifyAt = nullptr) {
  portENTER_CRITICAL(&pressensorMux);
  const float bar = pressensorBarShared;
  const uint32_t at = pressensorNotifyAtShared;
  portEXIT_CRITICAL(&pressensorMux);
  if (notifyAt != nullptr) {
    *notifyAt = at;
  }
  return bar;
}

static inline bool pressensorNameMatches(const char *name) {
  if (name == nullptr) {
    return false;
  }
  char upper[32];
  snprintf(upper, sizeof(upper), "%s", name);
  for (char *c = upper; *c != 0; c++) {
    *c = toupper(*c);
  }
  return strstr(upper, PRESSENSOR_NAME_MARKER) != nullptr;
}

static inline bool pressensorMacEquals(const char *a, const char *b) {
  return a[0] != 0 && b[0] != 0 && strcasecmp(a, b) == 0;
}

class PressensorScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (pressensorLink.matchFound) {
      return;
    }
    char name[24] = { 0 };
    char mac[18] = { 0 };
    snprintf(name, sizeof(name), "%s", advertisedDevice.getName().c_str());
    snprintf(mac, sizeof(mac), "%s", advertisedDevice.getAddress().toString().c_str());
    if (!pressensorNameMatches(name)) {
      return;
    }
    if (pressensorLink.scanListCount < PRESSENSOR_SCAN_LIST_MAX) {
      bool known = false;
      for (uint8_t i = 0; i < pressensorLink.scanListCount; i++) {
        if (pressensorMacEquals(pressensorLink.scanList[i].mac, mac)) {
          known = true;
          break;
        }
      }
      if (!known) {
        PressensorScanEntry &entry = pressensorLink.scanList[pressensorLink.scanListCount];
        snprintf(entry.mac, sizeof(entry.mac), "%s", mac);
        snprintf(entry.name, sizeof(entry.name), "%s", name[0] ? name : "PRS");
        entry.rssi = advertisedDevice.getRSSI();
        pressensorLink.scanListCount++;
      }
    }
    const bool wanted = pressensorLink.matchAnyDevice || pressensorMacEquals(pressensorLink.targetMac, mac);
    if (!wanted) {
      return;
    }
    memcpy(pressensorLink.peerAddr.val, advertisedDevice.getAddress().getNative(), 6);
    pressensorLink.peerAddr.type = advertisedDevice.getAddressType();
    pressensorLink.matchFound = true;
    advertisedDevice.getScan()->stop();
  }
};

PressensorScanCallbacks pressensorScanCallbacks;

static void pressensorScanComplete(BLEScanResults results) {
  pressensorLink.scanEnded = true;
}

static int pressensorGapEvent(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        pressensorLink.connHandle = event->connect.conn_handle;
        pressensorLink.phaseDone = true;
      } else {
        pressensorLink.phaseFailed = true;
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      if (pressensorLink.connHandle != BLE_HS_CONN_HANDLE_NONE &&
          event->disconnect.conn.conn_handle == pressensorLink.connHandle) {
        pressensorLink.connHandle = BLE_HS_CONN_HANDLE_NONE;
        pressensorLink.dropped = true;
      }
      return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
      if (event->notify_rx.conn_handle == pressensorLink.connHandle &&
          event->notify_rx.attr_handle == pressensorLink.pressureValHandle) {
        uint8_t payload[4] = { 0 };
        const uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len >= 2 && os_mbuf_copydata(event->notify_rx.om, 0, 2, payload) == 0) {
          const float bar = pressensorParseBar(payload, 2);
          if (!isnan(bar)) {
            pressensorStoreBar(bar);
          }
        }
      }
      return 0;
    default:
      return 0;
  }
}

static int pressensorSvcDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg) {
  if (error->status == 0 && service != nullptr) {
    pressensorLink.svcStartHandle = service->start_handle;
    pressensorLink.svcEndHandle = service->end_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE && pressensorLink.svcStartHandle != 0) {
    pressensorLink.phaseDone = true;
  } else {
    pressensorLink.phaseFailed = true;
  }
  return 0;
}

static int pressensorChrDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, const struct ble_gatt_chr *characteristic, void *arg) {
  if (error->status == 0 && characteristic != nullptr) {
    if (ble_uuid_cmp(&characteristic->uuid.u, &pressensorPressureUuid.u) == 0) {
      pressensorLink.pressureValHandle = characteristic->val_handle;
    } else if (ble_uuid_cmp(&characteristic->uuid.u, &pressensorZeroUuid.u) == 0) {
      pressensorLink.zeroValHandle = characteristic->val_handle;
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && pressensorLink.pressureValHandle != 0 && pressensorLink.zeroValHandle != 0) {
    pressensorLink.phaseDone = true;
  } else {
    pressensorLink.phaseFailed = true;
  }
  return 0;
}

static int pressensorDscDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, uint16_t chrValHandle, const struct ble_gatt_dsc *descriptor, void *arg) {
  if (error->status == 0 && descriptor != nullptr) {
    if (descriptor->uuid.u.type == BLE_UUID_TYPE_16 && descriptor->uuid.u16.value == 0x2902) {
      pressensorLink.pressureCccdHandle = descriptor->handle;
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && pressensorLink.pressureCccdHandle != 0) {
    pressensorLink.phaseDone = true;
  } else {
    pressensorLink.phaseFailed = true;
  }
  return 0;
}

static inline void pressensorEnterPhase(PressensorLinkState state) {
  pressensorLink.phaseDone = false;
  pressensorLink.phaseFailed = false;
  pressensorLink.phaseStartedAt = millis();
  pressensorLink.state = state;
}

static inline void pressensorDropLink(uint32_t retryDelayMs) {
  if (pressensorLink.state == PRESSENSOR_LINK_CONNECTING) {
    ble_gap_conn_cancel();
  }
  const uint16_t handle = pressensorLink.connHandle;
  if (handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
  }
  pressensorLink.connHandle = BLE_HS_CONN_HANDLE_NONE;
  pressensorLink.svcStartHandle = 0;
  pressensorLink.svcEndHandle = 0;
  pressensorLink.pressureValHandle = 0;
  pressensorLink.zeroValHandle = 0;
  pressensorLink.pressureCccdHandle = 0;
  pressensorLink.dropped = false;
  pressensorLink.matchFound = false;
  pressensorLink.nextScanAt = millis() + retryDelayMs;
  pressensorLink.state = PRESSENSOR_LINK_SCAN_WAIT;
}

static inline void pressensorLinkStop() {
  if (pressensorLink.state == PRESSENSOR_LINK_SCANNING && BLEDevice::getInitialized()) {
    BLEDevice::getScan()->stop();
  }
  pressensorDropLink(0);
  pressensorLink.scanEnded = false;
  pressensorLink.state = PRESSENSOR_LINK_OFF;
}

static inline void pressensorLinkBegin(const char *savedMac) {
  snprintf(pressensorLink.targetMac, sizeof(pressensorLink.targetMac), "%s", savedMac == nullptr ? "" : savedMac);
  pressensorLink.matchAnyDevice = pressensorLink.targetMac[0] == 0;
  pressensorLink.nextScanAt = millis();
  pressensorLink.state = PRESSENSOR_LINK_SCAN_WAIT;
}

static inline bool pressensorStartScan() {
  if (!BLEDevice::getInitialized()) {
    return false;
  }
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&pressensorScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  pressensorLink.scanListCount = 0;
  pressensorLink.matchFound = false;
  pressensorLink.scanEnded = false;
  return scan->start(PRESSENSOR_SCAN_SECONDS, pressensorScanComplete, false);
}

static inline bool pressensorZeroNow() {
  const uint16_t handle = pressensorLink.connHandle;
  if (handle == BLE_HS_CONN_HANDLE_NONE || pressensorLink.zeroValHandle == 0) {
    return false;
  }
  const uint8_t zero = 0x00;
  return ble_gattc_write_no_rsp_flat(handle, pressensorLink.zeroValHandle, &zero, 1) == 0;
}

static inline void pressensorStartConnect() {
  pressensorLink.matchFound = false;
  if (!BLEDevice::getInitialized()) {
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
    return;
  }
  char addr[18];
  snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
           pressensorLink.peerAddr.val[5], pressensorLink.peerAddr.val[4], pressensorLink.peerAddr.val[3],
           pressensorLink.peerAddr.val[2], pressensorLink.peerAddr.val[1], pressensorLink.peerAddr.val[0]);
  Serial.printf("[pressensor] connecting %s\n", addr);
  pressensorEnterPhase(PRESSENSOR_LINK_CONNECTING);
  int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &pressensorLink.peerAddr, 8000, nullptr, pressensorGapEvent, nullptr);
  if (rc == BLE_HS_EBUSY) {
    BLEDevice::getScan()->stop();
    rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &pressensorLink.peerAddr, 8000, nullptr, pressensorGapEvent, nullptr);
  }
  if (rc != 0) {
    Serial.printf("[pressensor] connect start failed rc=%d\n", rc);
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
  }
}

static inline void pressensorPhaseGuard() {
  if (pressensorLink.phaseFailed || millis() - pressensorLink.phaseStartedAt > PRESSENSOR_PHASE_TIMEOUT_MS) {
    Serial.printf("[pressensor] phase %d failed\n", (int)pressensorLink.state);
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
  }
}

static inline void pressensorLinkTick() {
  if (pressensorLink.state == PRESSENSOR_LINK_OFF) {
    return;
  }
  if (pressensorLink.dropped && pressensorLink.state != PRESSENSOR_LINK_SCAN_WAIT && pressensorLink.state != PRESSENSOR_LINK_SCANNING) {
    Serial.println("[pressensor] link lost");
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
    return;
  }
  switch (pressensorLink.state) {
    case PRESSENSOR_LINK_SCAN_WAIT:
      if (millis() >= pressensorLink.nextScanAt) {
        if (pressensorStartScan()) {
          pressensorLink.state = PRESSENSOR_LINK_SCANNING;
        } else {
          pressensorLink.nextScanAt = millis() + PRESSENSOR_RESCAN_DELAY_MS;
        }
      }
      break;
    case PRESSENSOR_LINK_SCANNING:
      if (pressensorLink.matchFound) {
        pressensorStartConnect();
      } else if (pressensorLink.scanEnded) {
        pressensorLink.scanEnded = false;
        pressensorLink.nextScanAt = millis() + PRESSENSOR_RESCAN_DELAY_MS;
        pressensorLink.state = PRESSENSOR_LINK_SCAN_WAIT;
      }
      break;
    case PRESSENSOR_LINK_CONNECTING:
      if (pressensorLink.phaseDone) {
        Serial.println("[pressensor] connected");
        pressensorEnterPhase(PRESSENSOR_LINK_SETTLING);
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_SETTLING:
      if (millis() - pressensorLink.phaseStartedAt >= PRESSENSOR_SETTLE_MS) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_SVC);
        pressensorLink.svcStartHandle = 0;
        pressensorLink.svcEndHandle = 0;
        if (ble_gattc_disc_svc_by_uuid(pressensorLink.connHandle, &pressensorSvcUuid.u, pressensorSvcDiscovered, nullptr) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      }
      break;
    case PRESSENSOR_LINK_DISC_SVC:
      if (pressensorLink.phaseDone) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_CHR);
        pressensorLink.pressureValHandle = 0;
        pressensorLink.zeroValHandle = 0;
        if (ble_gattc_disc_all_chrs(pressensorLink.connHandle, pressensorLink.svcStartHandle, pressensorLink.svcEndHandle, pressensorChrDiscovered, nullptr) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_DISC_CHR:
      if (pressensorLink.phaseDone) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_DSC);
        pressensorLink.pressureCccdHandle = 0;
        if (ble_gattc_disc_all_dscs(pressensorLink.connHandle, pressensorLink.pressureValHandle, pressensorLink.svcEndHandle, pressensorDscDiscovered, nullptr) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_DISC_DSC:
      if (pressensorLink.phaseDone) {
        pressensorEnterPhase(PRESSENSOR_LINK_SUBSCRIBING);
        const uint8_t enableNotify[2] = { 0x01, 0x00 };
        if (ble_gattc_write_flat(pressensorLink.connHandle, pressensorLink.pressureCccdHandle, enableNotify, sizeof(enableNotify), nullptr, nullptr) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
          break;
        }
        pressensorZeroNow();
        pressensorStoreBar(0.0f);
        Serial.println("[pressensor] streaming");
        pressensorLink.state = PRESSENSOR_LINK_STREAMING;
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_STREAMING:
      break;
    default:
      break;
  }
}

static inline bool pressensorStreaming() {
  if (pressensorLink.state != PRESSENSOR_LINK_STREAMING) {
    return false;
  }
  uint32_t notifyAt = 0;
  pressensorReadBar(&notifyAt);
  return millis() - notifyAt < PRESSENSOR_STALE_MS;
}

static inline uint8_t pressensorBlockingScan() {
  if (!BLEDevice::getInitialized()) {
    return 0;
  }
  pressensorLinkStop();
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&pressensorScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  pressensorLink.scanListCount = 0;
  pressensorLink.matchAnyDevice = false;
  pressensorLink.targetMac[0] = 0;
  pressensorLink.matchFound = false;
  pressensorLink.scanEnded = false;
  scan->start(PRESSENSOR_SCAN_SECONDS, false);
  scan->clearResults();
  for (uint8_t i = 0; i + 1 < pressensorLink.scanListCount; i++) {
    for (uint8_t j = i + 1; j < pressensorLink.scanListCount; j++) {
      if (pressensorLink.scanList[j].rssi > pressensorLink.scanList[i].rssi) {
        const PressensorScanEntry swap = pressensorLink.scanList[i];
        pressensorLink.scanList[i] = pressensorLink.scanList[j];
        pressensorLink.scanList[j] = swap;
      }
    }
  }
  return pressensorLink.scanListCount;
}

#endif
