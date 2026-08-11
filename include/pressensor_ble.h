#ifndef PRESSENSOR_BLE_H
#define PRESSENSOR_BLE_H

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <string.h>
#if defined(CONFIG_NIMBLE_ENABLED)
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#endif

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
  unsigned long scanWaitStartedAt = 0;
  unsigned long scanWaitMs = 0;
  unsigned long phaseStartedAt = 0;
  ble_addr_t peerAddr = {};
  volatile bool scanActive = false;
  volatile bool matchFound = false;
  volatile bool scanEnded = false;
  volatile bool dropped = false;
  volatile bool phaseDone = false;
  volatile bool phaseFailed = false;
  volatile uint32_t generation = 0;
  volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
  volatile uint16_t svcStartHandle = 0;
  volatile uint16_t svcEndHandle = 0;
  volatile uint16_t pressureValHandle = 0;
  volatile uint16_t zeroValHandle = 0;
  volatile uint16_t pressureCccdHandle = 0;
  bool connectedOnce = false;
};

PressensorLink pressensorLink;
portMUX_TYPE pressensorMux = portMUX_INITIALIZER_UNLOCKED;
volatile float pressensorBarShared = 0.0f;
volatile unsigned long pressensorNotifyAtShared = 0;

static const ble_uuid128_t pressensorSvcUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2a, 0xe8, 0x3a, 0x87);
static const ble_uuid128_t pressensorPressureUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2b, 0xe8, 0x3a, 0x87);
static const ble_uuid128_t pressensorZeroUuid = BLE_UUID128_INIT(
  0xd0, 0xeb, 0xf7, 0x0b, 0x90, 0x9d, 0x39, 0xb5, 0x42, 0x43, 0x5a, 0x4c, 0x2c, 0xe8, 0x3a, 0x87);

static inline void *pressensorGenerationArg(uint32_t generation) {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(generation));
}

static inline uint32_t pressensorGenerationFromArg(void *arg) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
}

static inline float pressensorParseBar(const uint8_t *data, size_t length) {
  if (data == nullptr || length < 2) {
    return NAN;
  }
  const uint16_t encoded = (static_cast<uint16_t>(data[0]) << 8) | data[1];
  return static_cast<int16_t>(encoded) / 1000.0f;
}

static inline void pressensorStoreBar(float bar) {
  const unsigned long now = millis();
  portENTER_CRITICAL(&pressensorMux);
  pressensorBarShared = bar;
  pressensorNotifyAtShared = now;
  portEXIT_CRITICAL(&pressensorMux);
}

static inline void pressensorClearBar() {
  portENTER_CRITICAL(&pressensorMux);
  pressensorBarShared = 0.0f;
  pressensorNotifyAtShared = 0;
  portEXIT_CRITICAL(&pressensorMux);
}

static inline float pressensorReadBar(unsigned long *notifyAt = nullptr) {
  portENTER_CRITICAL(&pressensorMux);
  const float bar = pressensorBarShared;
  const unsigned long at = pressensorNotifyAtShared;
  portEXIT_CRITICAL(&pressensorMux);
  if (notifyAt != nullptr) {
    *notifyAt = at;
  }
  return bar;
}

static inline PressensorLinkState pressensorGetLinkState() {
  portENTER_CRITICAL(&pressensorMux);
  const PressensorLinkState state = pressensorLink.state;
  portEXIT_CRITICAL(&pressensorMux);
  return state;
}

static inline uint8_t pressensorScanListCount() {
  portENTER_CRITICAL(&pressensorMux);
  const uint8_t count = pressensorLink.scanListCount;
  portEXIT_CRITICAL(&pressensorMux);
  return count;
}

static inline bool pressensorCopyScanEntry(uint8_t index, PressensorScanEntry *entry) {
  if (entry == nullptr) {
    return false;
  }
  portENTER_CRITICAL(&pressensorMux);
  const bool found = index < pressensorLink.scanListCount;
  if (found) {
    *entry = pressensorLink.scanList[index];
  }
  portEXIT_CRITICAL(&pressensorMux);
  return found;
}

static inline bool pressensorNameMatches(const char *name) {
  if (name == nullptr) {
    return false;
  }
  char upper[32];
  snprintf(upper, sizeof(upper), "%s", name);
  for (char *c = upper; *c != 0; c++) {
    *c = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
  }
  return strstr(upper, PRESSENSOR_NAME_MARKER) != nullptr;
}

static inline bool pressensorMacEquals(const char *a, const char *b) {
  return a[0] != 0 && b[0] != 0 && strcasecmp(a, b) == 0;
}

class PressensorScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    PressensorScanEntry candidate;
    snprintf(candidate.name, sizeof(candidate.name), "%s", advertisedDevice.getName().c_str());
    snprintf(candidate.mac, sizeof(candidate.mac), "%s", advertisedDevice.getAddress().toString().c_str());
    candidate.rssi = advertisedDevice.getRSSI();
    if (!pressensorNameMatches(candidate.name)) {
      return;
    }
    ble_addr_t peerAddr = {};
    memcpy(peerAddr.val, advertisedDevice.getAddress().getNative(), 6);
    peerAddr.type = advertisedDevice.getAddressType();

    bool wanted = false;
    portENTER_CRITICAL(&pressensorMux);
    if (pressensorLink.scanActive) {
      bool known = false;
      for (uint8_t i = 0; i < pressensorLink.scanListCount; i++) {
        if (pressensorMacEquals(pressensorLink.scanList[i].mac, candidate.mac)) {
          known = true;
          break;
        }
      }
      if (!known && pressensorLink.scanListCount < PRESSENSOR_SCAN_LIST_MAX) {
        pressensorLink.scanList[pressensorLink.scanListCount] = candidate;
        pressensorLink.scanListCount++;
      }
      wanted = !pressensorLink.matchFound && pressensorMacEquals(pressensorLink.targetMac, candidate.mac);
      if (wanted) {
        pressensorLink.peerAddr = peerAddr;
        pressensorLink.matchFound = true;
        pressensorLink.scanActive = false;
      }
    }
    portEXIT_CRITICAL(&pressensorMux);

    if (wanted) {
      advertisedDevice.getScan()->stop();
    }
  }
};

PressensorScanCallbacks pressensorScanCallbacks;

static void pressensorScanComplete(BLEScanResults) {
  portENTER_CRITICAL(&pressensorMux);
  if (pressensorLink.scanActive) {
    pressensorLink.scanActive = false;
    pressensorLink.scanEnded = true;
  }
  portEXIT_CRITICAL(&pressensorMux);
}

static int pressensorGapEvent(struct ble_gap_event *event, void *arg) {
  const uint32_t generation = pressensorGenerationFromArg(arg);
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      portENTER_CRITICAL(&pressensorMux);
      if (generation == pressensorLink.generation) {
        if (event->connect.status == 0) {
          pressensorLink.connHandle = event->connect.conn_handle;
          pressensorLink.phaseDone = true;
        } else {
          pressensorLink.phaseFailed = true;
        }
      }
      portEXIT_CRITICAL(&pressensorMux);
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      portENTER_CRITICAL(&pressensorMux);
      if (generation == pressensorLink.generation &&
          event->disconnect.conn.conn_handle == pressensorLink.connHandle) {
        pressensorLink.connHandle = BLE_HS_CONN_HANDLE_NONE;
        pressensorLink.dropped = true;
      }
      portEXIT_CRITICAL(&pressensorMux);
      return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
      portENTER_CRITICAL(&pressensorMux);
      const bool current = generation == pressensorLink.generation &&
                           event->notify_rx.conn_handle == pressensorLink.connHandle &&
                           event->notify_rx.attr_handle == pressensorLink.pressureValHandle;
      portEXIT_CRITICAL(&pressensorMux);
      if (!current) {
        return 0;
      }
      uint8_t payload[2] = { 0 };
      const uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
      if (length >= sizeof(payload) && os_mbuf_copydata(event->notify_rx.om, 0, sizeof(payload), payload) == 0) {
        const float bar = pressensorParseBar(payload, sizeof(payload));
        if (!isnan(bar)) {
          pressensorStoreBar(bar);
        }
      }
      return 0;
    }
    default:
      return 0;
  }
}

static int pressensorSvcDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg) {
  const uint32_t generation = pressensorGenerationFromArg(arg);
  portENTER_CRITICAL(&pressensorMux);
  if (generation == pressensorLink.generation && connHandle == pressensorLink.connHandle) {
    if (error->status == 0 && service != nullptr) {
      pressensorLink.svcStartHandle = service->start_handle;
      pressensorLink.svcEndHandle = service->end_handle;
    } else if (error->status == BLE_HS_EDONE && pressensorLink.svcStartHandle != 0) {
      pressensorLink.phaseDone = true;
    } else {
      pressensorLink.phaseFailed = true;
    }
  }
  portEXIT_CRITICAL(&pressensorMux);
  return 0;
}

static int pressensorChrDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, const struct ble_gatt_chr *characteristic, void *arg) {
  const uint32_t generation = pressensorGenerationFromArg(arg);
  portENTER_CRITICAL(&pressensorMux);
  if (generation == pressensorLink.generation && connHandle == pressensorLink.connHandle) {
    if (error->status == 0 && characteristic != nullptr) {
      if (ble_uuid_cmp(&characteristic->uuid.u, &pressensorPressureUuid.u) == 0) {
        pressensorLink.pressureValHandle = characteristic->val_handle;
      } else if (ble_uuid_cmp(&characteristic->uuid.u, &pressensorZeroUuid.u) == 0) {
        pressensorLink.zeroValHandle = characteristic->val_handle;
      }
    } else if (error->status == BLE_HS_EDONE && pressensorLink.pressureValHandle != 0 && pressensorLink.zeroValHandle != 0) {
      pressensorLink.phaseDone = true;
    } else {
      pressensorLink.phaseFailed = true;
    }
  }
  portEXIT_CRITICAL(&pressensorMux);
  return 0;
}

static int pressensorDscDiscovered(uint16_t connHandle, const struct ble_gatt_error *error, uint16_t chrValHandle, const struct ble_gatt_dsc *descriptor, void *arg) {
  const uint32_t generation = pressensorGenerationFromArg(arg);
  portENTER_CRITICAL(&pressensorMux);
  if (generation == pressensorLink.generation && connHandle == pressensorLink.connHandle) {
    if (error->status == 0 && descriptor != nullptr) {
      if (chrValHandle == pressensorLink.pressureValHandle &&
          descriptor->uuid.u.type == BLE_UUID_TYPE_16 && descriptor->uuid.u16.value == 0x2902) {
        pressensorLink.pressureCccdHandle = descriptor->handle;
      }
    } else if (error->status == BLE_HS_EDONE && pressensorLink.pressureCccdHandle != 0) {
      pressensorLink.phaseDone = true;
    } else {
      pressensorLink.phaseFailed = true;
    }
  }
  portEXIT_CRITICAL(&pressensorMux);
  return 0;
}

static int pressensorSubscriptionWritten(uint16_t connHandle, const struct ble_gatt_error *error, struct ble_gatt_attr *, void *arg) {
  const uint32_t generation = pressensorGenerationFromArg(arg);
  portENTER_CRITICAL(&pressensorMux);
  if (generation == pressensorLink.generation && connHandle == pressensorLink.connHandle) {
    if (error->status == 0) {
      pressensorLink.phaseDone = true;
    } else {
      pressensorLink.phaseFailed = true;
    }
  }
  portEXIT_CRITICAL(&pressensorMux);
  return 0;
}

static inline void pressensorEnterPhase(PressensorLinkState state) {
  const unsigned long now = millis();
  portENTER_CRITICAL(&pressensorMux);
  pressensorLink.phaseDone = false;
  pressensorLink.phaseFailed = false;
  pressensorLink.phaseStartedAt = now;
  pressensorLink.state = state;
  portEXIT_CRITICAL(&pressensorMux);
}

static inline void pressensorDropLink(unsigned long retryDelayMs) {
  const unsigned long now = millis();
  bool cancelConnect;
  uint16_t handle;
  portENTER_CRITICAL(&pressensorMux);
  cancelConnect = pressensorLink.state == PRESSENSOR_LINK_CONNECTING;
  handle = pressensorLink.connHandle;
  pressensorLink.generation = pressensorLink.generation + 1;
  pressensorLink.connHandle = BLE_HS_CONN_HANDLE_NONE;
  pressensorLink.svcStartHandle = 0;
  pressensorLink.svcEndHandle = 0;
  pressensorLink.pressureValHandle = 0;
  pressensorLink.zeroValHandle = 0;
  pressensorLink.pressureCccdHandle = 0;
  pressensorLink.scanActive = false;
  pressensorLink.dropped = false;
  pressensorLink.matchFound = false;
  pressensorLink.phaseDone = false;
  pressensorLink.phaseFailed = false;
  pressensorLink.scanWaitStartedAt = now;
  pressensorLink.scanWaitMs = retryDelayMs;
  pressensorLink.state = PRESSENSOR_LINK_SCAN_WAIT;
  portEXIT_CRITICAL(&pressensorMux);

  pressensorClearBar();
  if (cancelConnect) {
    ble_gap_conn_cancel();
  }
  if (handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
  }
}

static inline void pressensorLinkStop() {
  bool stopScan;
  portENTER_CRITICAL(&pressensorMux);
  stopScan = pressensorLink.state == PRESSENSOR_LINK_SCANNING;
  pressensorLink.scanActive = false;
  portEXIT_CRITICAL(&pressensorMux);
  if (stopScan && BLEDevice::getInitialized()) {
    BLEDevice::getScan()->stop();
  }
  pressensorDropLink(0);
  portENTER_CRITICAL(&pressensorMux);
  pressensorLink.scanEnded = false;
  pressensorLink.state = PRESSENSOR_LINK_OFF;
  portEXIT_CRITICAL(&pressensorMux);
}

static inline void pressensorLinkBegin(const char *savedMac) {
  pressensorLinkStop();
  const unsigned long now = millis();
  portENTER_CRITICAL(&pressensorMux);
  snprintf(pressensorLink.targetMac, sizeof(pressensorLink.targetMac), "%s", savedMac == nullptr ? "" : savedMac);
  pressensorLink.scanWaitStartedAt = now;
  pressensorLink.scanWaitMs = 0;
  pressensorLink.state = pressensorLink.targetMac[0] == 0 ? PRESSENSOR_LINK_OFF : PRESSENSOR_LINK_SCAN_WAIT;
  portEXIT_CRITICAL(&pressensorMux);
}

static inline bool pressensorStartScan() {
  if (!BLEDevice::getInitialized()) {
    return false;
  }
  portENTER_CRITICAL(&pressensorMux);
  const bool hasTarget = pressensorLink.targetMac[0] != 0;
  if (hasTarget) {
    pressensorLink.scanListCount = 0;
    pressensorLink.matchFound = false;
    pressensorLink.scanEnded = false;
    pressensorLink.scanActive = true;
  }
  portEXIT_CRITICAL(&pressensorMux);
  if (!hasTarget) {
    return false;
  }

  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&pressensorScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(160);
  scan->setWindow(80);
  const bool started = scan->start(PRESSENSOR_SCAN_SECONDS, pressensorScanComplete, false);
  if (!started) {
    portENTER_CRITICAL(&pressensorMux);
    pressensorLink.scanActive = false;
    portEXIT_CRITICAL(&pressensorMux);
  }
  return started;
}

static inline bool pressensorZeroNow() {
  uint16_t handle;
  uint16_t zeroHandle;
  portENTER_CRITICAL(&pressensorMux);
  handle = pressensorLink.connHandle;
  zeroHandle = pressensorLink.zeroValHandle;
  portEXIT_CRITICAL(&pressensorMux);
  if (handle == BLE_HS_CONN_HANDLE_NONE || zeroHandle == 0) {
    return false;
  }
  const uint8_t zero = 0x00;
  return ble_gattc_write_no_rsp_flat(handle, zeroHandle, &zero, 1) == 0;
}

static inline void pressensorStartConnect() {
  ble_addr_t peerAddr = {};
  uint32_t generation;
  const unsigned long now = millis();
  portENTER_CRITICAL(&pressensorMux);
  peerAddr = pressensorLink.peerAddr;
  pressensorLink.matchFound = false;
  pressensorLink.scanActive = false;
  pressensorLink.generation = pressensorLink.generation + 1;
  generation = pressensorLink.generation;
  pressensorLink.connHandle = BLE_HS_CONN_HANDLE_NONE;
  pressensorLink.phaseDone = false;
  pressensorLink.phaseFailed = false;
  pressensorLink.phaseStartedAt = now;
  pressensorLink.state = PRESSENSOR_LINK_CONNECTING;
  portEXIT_CRITICAL(&pressensorMux);

  if (!BLEDevice::getInitialized()) {
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
    return;
  }
  char addr[18];
  snprintf(addr, sizeof(addr), "%02x:%02x:%02x:%02x:%02x:%02x",
           peerAddr.val[5], peerAddr.val[4], peerAddr.val[3], peerAddr.val[2], peerAddr.val[1], peerAddr.val[0]);
  Serial.printf("[pressensor] connecting %s\n", addr);
  int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peerAddr, 8000, nullptr, pressensorGapEvent, pressensorGenerationArg(generation));
  if (rc == BLE_HS_EBUSY) {
    BLEDevice::getScan()->stop();
    rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &peerAddr, 8000, nullptr, pressensorGapEvent, pressensorGenerationArg(generation));
  }
  if (rc != 0) {
    Serial.printf("[pressensor] connect start failed rc=%d\n", rc);
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
  }
}

static inline bool pressensorPhaseComplete() {
  portENTER_CRITICAL(&pressensorMux);
  const bool done = pressensorLink.phaseDone;
  portEXIT_CRITICAL(&pressensorMux);
  return done;
}

static inline void pressensorPhaseGuard() {
  bool failed;
  unsigned long startedAt;
  PressensorLinkState state;
  portENTER_CRITICAL(&pressensorMux);
  failed = pressensorLink.phaseFailed;
  startedAt = pressensorLink.phaseStartedAt;
  state = pressensorLink.state;
  portEXIT_CRITICAL(&pressensorMux);
  if (failed || millis() - startedAt > PRESSENSOR_PHASE_TIMEOUT_MS) {
    Serial.printf("[pressensor] phase %d failed\n", static_cast<int>(state));
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
  }
}

static inline void pressensorLinkTick() {
  PressensorLinkState state;
  bool dropped;
  portENTER_CRITICAL(&pressensorMux);
  state = pressensorLink.state;
  dropped = pressensorLink.dropped;
  portEXIT_CRITICAL(&pressensorMux);
  if (state == PRESSENSOR_LINK_OFF) {
    return;
  }
  if (dropped && state != PRESSENSOR_LINK_SCAN_WAIT && state != PRESSENSOR_LINK_SCANNING) {
    Serial.println("[pressensor] link lost");
    pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
    return;
  }

  switch (state) {
    case PRESSENSOR_LINK_SCAN_WAIT: {
      unsigned long waitStartedAt;
      unsigned long waitMs;
      portENTER_CRITICAL(&pressensorMux);
      waitStartedAt = pressensorLink.scanWaitStartedAt;
      waitMs = pressensorLink.scanWaitMs;
      portEXIT_CRITICAL(&pressensorMux);
      if (millis() - waitStartedAt >= waitMs) {
        if (pressensorStartScan()) {
          portENTER_CRITICAL(&pressensorMux);
          pressensorLink.state = PRESSENSOR_LINK_SCANNING;
          portEXIT_CRITICAL(&pressensorMux);
        } else {
          const unsigned long now = millis();
          portENTER_CRITICAL(&pressensorMux);
          pressensorLink.scanWaitStartedAt = now;
          pressensorLink.scanWaitMs = PRESSENSOR_RESCAN_DELAY_MS;
          portEXIT_CRITICAL(&pressensorMux);
        }
      }
      break;
    }
    case PRESSENSOR_LINK_SCANNING: {
      bool matchFound;
      bool scanEnded;
      portENTER_CRITICAL(&pressensorMux);
      matchFound = pressensorLink.matchFound;
      scanEnded = pressensorLink.scanEnded;
      portEXIT_CRITICAL(&pressensorMux);
      if (matchFound) {
        pressensorStartConnect();
      } else if (scanEnded) {
        const unsigned long now = millis();
        portENTER_CRITICAL(&pressensorMux);
        pressensorLink.scanEnded = false;
        pressensorLink.scanWaitStartedAt = now;
        pressensorLink.scanWaitMs = PRESSENSOR_RESCAN_DELAY_MS;
        pressensorLink.state = PRESSENSOR_LINK_SCAN_WAIT;
        portEXIT_CRITICAL(&pressensorMux);
      }
      break;
    }
    case PRESSENSOR_LINK_CONNECTING:
      if (pressensorPhaseComplete()) {
        Serial.println("[pressensor] connected");
        pressensorEnterPhase(PRESSENSOR_LINK_SETTLING);
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_SETTLING: {
      unsigned long startedAt;
      portENTER_CRITICAL(&pressensorMux);
      startedAt = pressensorLink.phaseStartedAt;
      portEXIT_CRITICAL(&pressensorMux);
      if (millis() - startedAt >= PRESSENSOR_SETTLE_MS) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_SVC);
        uint16_t handle;
        uint32_t generation;
        portENTER_CRITICAL(&pressensorMux);
        pressensorLink.svcStartHandle = 0;
        pressensorLink.svcEndHandle = 0;
        handle = pressensorLink.connHandle;
        generation = pressensorLink.generation;
        portEXIT_CRITICAL(&pressensorMux);
        if (ble_gattc_disc_svc_by_uuid(handle, &pressensorSvcUuid.u, pressensorSvcDiscovered, pressensorGenerationArg(generation)) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      }
      break;
    }
    case PRESSENSOR_LINK_DISC_SVC:
      if (pressensorPhaseComplete()) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_CHR);
        uint16_t handle;
        uint16_t startHandle;
        uint16_t endHandle;
        uint32_t generation;
        portENTER_CRITICAL(&pressensorMux);
        pressensorLink.pressureValHandle = 0;
        pressensorLink.zeroValHandle = 0;
        handle = pressensorLink.connHandle;
        startHandle = pressensorLink.svcStartHandle;
        endHandle = pressensorLink.svcEndHandle;
        generation = pressensorLink.generation;
        portEXIT_CRITICAL(&pressensorMux);
        if (ble_gattc_disc_all_chrs(handle, startHandle, endHandle, pressensorChrDiscovered, pressensorGenerationArg(generation)) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_DISC_CHR:
      if (pressensorPhaseComplete()) {
        pressensorEnterPhase(PRESSENSOR_LINK_DISC_DSC);
        uint16_t handle;
        uint16_t valueHandle;
        uint16_t endHandle;
        uint32_t generation;
        portENTER_CRITICAL(&pressensorMux);
        pressensorLink.pressureCccdHandle = 0;
        handle = pressensorLink.connHandle;
        valueHandle = pressensorLink.pressureValHandle;
        endHandle = pressensorLink.svcEndHandle;
        generation = pressensorLink.generation;
        portEXIT_CRITICAL(&pressensorMux);
        if (ble_gattc_disc_all_dscs(handle, valueHandle, endHandle, pressensorDscDiscovered, pressensorGenerationArg(generation)) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_DISC_DSC:
      if (pressensorPhaseComplete()) {
        pressensorEnterPhase(PRESSENSOR_LINK_SUBSCRIBING);
        pressensorClearBar();
        uint16_t handle;
        uint16_t cccdHandle;
        uint32_t generation;
        portENTER_CRITICAL(&pressensorMux);
        handle = pressensorLink.connHandle;
        cccdHandle = pressensorLink.pressureCccdHandle;
        generation = pressensorLink.generation;
        portEXIT_CRITICAL(&pressensorMux);
        const uint8_t enableNotify[2] = { 0x01, 0x00 };
        if (ble_gattc_write_flat(handle, cccdHandle, enableNotify, sizeof(enableNotify), pressensorSubscriptionWritten,
                                 pressensorGenerationArg(generation)) != 0) {
          pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
        }
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_SUBSCRIBING:
      if (pressensorPhaseComplete()) {
        bool zeroInitialConnection;
        portENTER_CRITICAL(&pressensorMux);
        zeroInitialConnection = !pressensorLink.connectedOnce;
        pressensorLink.connectedOnce = true;
        portEXIT_CRITICAL(&pressensorMux);
        if (zeroInitialConnection) {
          pressensorZeroNow();
        }
        pressensorEnterPhase(PRESSENSOR_LINK_STREAMING);
        Serial.println("[pressensor] streaming");
      } else {
        pressensorPhaseGuard();
      }
      break;
    case PRESSENSOR_LINK_STREAMING: {
      unsigned long notifyAt = 0;
      pressensorReadBar(&notifyAt);
      unsigned long startedAt;
      portENTER_CRITICAL(&pressensorMux);
      startedAt = pressensorLink.phaseStartedAt;
      portEXIT_CRITICAL(&pressensorMux);
      const unsigned long now = millis();
      if ((notifyAt == 0 && now - startedAt > PRESSENSOR_PHASE_TIMEOUT_MS) ||
          (notifyAt != 0 && now - notifyAt >= PRESSENSOR_STALE_MS)) {
        Serial.println("[pressensor] stream stale");
        pressensorDropLink(PRESSENSOR_RESCAN_DELAY_MS);
      }
      break;
    }
    default:
      break;
  }
}

static inline bool pressensorStreaming() {
  if (pressensorGetLinkState() != PRESSENSOR_LINK_STREAMING) {
    return false;
  }
  unsigned long notifyAt = 0;
  pressensorReadBar(&notifyAt);
  return notifyAt != 0 && millis() - notifyAt < PRESSENSOR_STALE_MS;
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
  portENTER_CRITICAL(&pressensorMux);
  pressensorLink.scanListCount = 0;
  pressensorLink.targetMac[0] = 0;
  pressensorLink.matchFound = false;
  pressensorLink.scanEnded = false;
  pressensorLink.scanActive = true;
  portEXIT_CRITICAL(&pressensorMux);
  scan->start(PRESSENSOR_SCAN_SECONDS, false);
  portENTER_CRITICAL(&pressensorMux);
  pressensorLink.scanActive = false;
  for (uint8_t i = 0; i + 1 < pressensorLink.scanListCount; i++) {
    for (uint8_t j = i + 1; j < pressensorLink.scanListCount; j++) {
      if (pressensorLink.scanList[j].rssi > pressensorLink.scanList[i].rssi) {
        const PressensorScanEntry swap = pressensorLink.scanList[i];
        pressensorLink.scanList[i] = pressensorLink.scanList[j];
        pressensorLink.scanList[j] = swap;
      }
    }
  }
  const uint8_t count = pressensorLink.scanListCount;
  portEXIT_CRITICAL(&pressensorMux);
  scan->clearResults();
  return count;
}

#endif
