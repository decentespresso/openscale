#ifndef GRINDER_BLE_TRANSPORT_H
#define GRINDER_BLE_TRANSPORT_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <nimble/nimble_npl.h>
#include <nimble/nimble_port.h>
#include <strings.h>
#include "grinder_ble_transport_state.h"

extern GrinderBleTransport grinderBleTransport;
extern portMUX_TYPE grinderBleMux;

static constexpr unsigned long grinderBlePhaseTimeoutMs = 6000;
static constexpr unsigned long grinderBleCloseRetryMs = 250;
static constexpr int32_t grinderBleScanMs = 3000;

static const ble_uuid128_t grinderBleServiceUuid = BLE_UUID128_INIT(
    0x01, 0x78, 0x9d, 0x3b, 0xe2, 0xf5, 0x2b, 0xbb, 0x8e, 0x4d, 0x61, 0x6d, 0x00, 0x54, 0x73, 0x6f);
static const ble_uuid128_t grinderBleCommandUuid = BLE_UUID128_INIT(
    0x01, 0x78, 0x9d, 0x3b, 0xe2, 0xf5, 0x2b, 0xbb, 0x8e, 0x4d, 0x61, 0x6d, 0x01, 0x54, 0x73, 0x6f);
static const ble_uuid128_t grinderBleReplyUuid = BLE_UUID128_INIT(
    0x01, 0x78, 0x9d, 0x3b, 0xe2, 0xf5, 0x2b, 0xbb, 0x8e, 0x4d, 0x61, 0x6d, 0x02, 0x54, 0x73, 0x6f);

static bool grinderBleAvailable() {
  return BLEDevice::getInitialized() && ble_hs_synced();
}

static uint32_t grinderBleGeneration(void *arg) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
}

static void *grinderBleGenerationArg() {
  return reinterpret_cast<void *>(static_cast<uintptr_t>(grinderBleTransport.generation));
}

static void grinderBleSetPhaseLocked(GrinderBlePhase phase) {
  grinderBleTransport.phase = phase;
  grinderBleTransport.phaseAt = millis();
}

static void grinderBleResetOnHost() {
  portENTER_CRITICAL(&grinderBleMux);
  grinderBleTransport.reset();
  portEXIT_CRITICAL(&grinderBleMux);
}

static void grinderBleCloseOnHost() {
  portENTER_CRITICAL(&grinderBleMux);
  grinderBleTransport.closeRequested = true;
  grinderBleTransport.closeAt = millis();
  const GrinderBlePhase phase = grinderBleTransport.phase;
  const uint16_t handle = grinderBleTransport.handle;
  if (handle != grinderBleNoConnection) grinderBleSetPhaseLocked(GrinderBlePhase::Closing);
  portEXIT_CRITICAL(&grinderBleMux);

  if (handle != grinderBleNoConnection) {
    if (ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM) == BLE_HS_ENOTCONN) grinderBleResetOnHost();
  } else if (phase == GrinderBlePhase::Connecting) {
    ble_gap_conn_cancel();
  } else if (phase == GrinderBlePhase::Scanning) {
    const int result = ble_gap_disc_cancel();
    if (result == 0 || result == BLE_HS_EALREADY) grinderBleResetOnHost();
  } else {
    grinderBleResetOnHost();
  }
}

static bool grinderBleCallbackCurrent(GrinderBlePhase phase, uint16_t handle, void *arg) {
  portENTER_CRITICAL(&grinderBleMux);
  const bool current = grinderBleTransport.current(grinderBleGeneration(arg), handle) &&
                       grinderBleTransport.phase == phase && !grinderBleTransport.closeRequested;
  portEXIT_CRITICAL(&grinderBleMux);
  return current;
}

static void grinderBleAdvanceOnHost(GrinderBlePhase phase) {
  portENTER_CRITICAL(&grinderBleMux);
  grinderBleSetPhaseLocked(phase);
  portEXIT_CRITICAL(&grinderBleMux);
}

static int grinderBleSubscriptionWritten(uint16_t handle, const ble_gatt_error *error, ble_gatt_attr *, void *arg) {
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Subscribe, handle, arg)) return 0;
  if (error->status == 0) grinderBleAdvanceOnHost(GrinderBlePhase::Ready);
  else grinderBleCloseOnHost();
  return 0;
}

static int grinderBleDescriptorFound(uint16_t handle, const ble_gatt_error *error, uint16_t valueHandle,
                                     const ble_gatt_dsc *descriptor, void *arg) {
  auto &link = grinderBleTransport;
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Descriptors, handle, arg)) return BLE_HS_EAPP;
  if (error->status == 0 && descriptor != nullptr) {
    if (valueHandle == link.replyHandle && ble_uuid_u16(&descriptor->uuid.u) == 0x2902) {
      if (link.subscriptionHandle != 0) {
        grinderBleCloseOnHost();
        return BLE_HS_EAPP;
      }
      link.subscriptionHandle = descriptor->handle;
    }
    return 0;
  }

  if (error->status == BLE_HS_EDONE && link.subscriptionHandle != 0) {
    const uint8_t notifyOn[] = {1, 0};
    grinderBleAdvanceOnHost(GrinderBlePhase::Subscribe);
    if (ble_gattc_write_flat(handle, link.subscriptionHandle, notifyOn, sizeof(notifyOn),
                            grinderBleSubscriptionWritten, arg) == 0) return 0;
  }
  grinderBleCloseOnHost();
  return 0;
}

static int grinderBleCharacteristicFound(uint16_t handle, const ble_gatt_error *error,
                                         const ble_gatt_chr *characteristic, void *arg) {
  auto &link = grinderBleTransport;
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Characteristics, handle, arg)) return BLE_HS_EAPP;
  if (error->status == 0 && characteristic != nullptr) {
    if (link.replyHandle != 0 && characteristic->def_handle > link.replyHandle &&
        characteristic->def_handle <= link.replyEnd) {
      link.replyEnd = characteristic->def_handle - 1;
    }
    if (ble_uuid_cmp(&characteristic->uuid.u, &grinderBleCommandUuid.u) == 0) {
      if (link.commandHandle != 0 || !(characteristic->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP)) {
        grinderBleCloseOnHost();
        return BLE_HS_EAPP;
      }
      link.commandHandle = characteristic->val_handle;
    } else if (ble_uuid_cmp(&characteristic->uuid.u, &grinderBleReplyUuid.u) == 0) {
      if (link.replyHandle != 0 || !(characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
        grinderBleCloseOnHost();
        return BLE_HS_EAPP;
      }
      link.replyHandle = characteristic->val_handle;
      link.replyEnd = link.serviceEnd;
    }
    return 0;
  }

  if (error->status == BLE_HS_EDONE && link.commandHandle != 0 && link.replyHandle != 0 &&
      link.replyHandle < link.replyEnd) {
    grinderBleAdvanceOnHost(GrinderBlePhase::Descriptors);
    if (ble_gattc_disc_all_dscs(handle, link.replyHandle, link.replyEnd, grinderBleDescriptorFound, arg) == 0) return 0;
  }
  grinderBleCloseOnHost();
  return 0;
}

static int grinderBleServiceFound(uint16_t handle, const ble_gatt_error *error, const ble_gatt_svc *service, void *arg) {
  auto &link = grinderBleTransport;
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Service, handle, arg)) return BLE_HS_EAPP;
  if (error->status == 0 && service != nullptr) {
    if (link.serviceStart != 0) {
      grinderBleCloseOnHost();
      return BLE_HS_EAPP;
    }
    link.serviceStart = service->start_handle;
    link.serviceEnd = service->end_handle;
    return 0;
  }

  if (error->status == BLE_HS_EDONE && link.serviceStart != 0 && link.serviceEnd > link.serviceStart) {
    grinderBleAdvanceOnHost(GrinderBlePhase::Characteristics);
    if (ble_gattc_disc_all_chrs(handle, link.serviceStart, link.serviceEnd, grinderBleCharacteristicFound, arg) == 0) return 0;
  }
  grinderBleCloseOnHost();
  return 0;
}

static void grinderBleReceiveOnHost(const ble_gap_event *event, void *arg) {
  auto &link = grinderBleTransport;
  if (event->notify_rx.indication || event->notify_rx.attr_handle != link.replyHandle ||
      event->notify_rx.om == nullptr) return;
  const uint16_t handle = event->notify_rx.conn_handle;
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Subscribe, handle, arg) &&
      !grinderBleCallbackCurrent(GrinderBlePhase::Ready, handle, arg)) return;

  uint8_t packet[grinderBleFragmentSize + 1];
  uint16_t size = 0;
  if (ble_hs_mbuf_to_flat(event->notify_rx.om, packet, sizeof(packet), &size) != 0) {
    grinderBleCloseOnHost();
    return;
  }
  portENTER_CRITICAL(&grinderBleMux);
  const bool queued = link.closeRequested || link.queueReply(packet, size);
  portEXIT_CRITICAL(&grinderBleMux);
  if (!queued) grinderBleCloseOnHost();
}

static int grinderBleLinkEvent(ble_gap_event *event, void *arg) {
  auto &link = grinderBleTransport;
  const bool connectEvent = event->type == BLE_GAP_EVENT_CONNECT;
  bool outgoingConnected = false;
  if (connectEvent && event->connect.status == 0) {
    ble_gap_conn_desc peer;
    outgoingConnected = ble_gap_conn_find(event->connect.conn_handle, &peer) == 0 && peer.role == BLE_GAP_ROLE_MASTER;
  }
  portENTER_CRITICAL(&grinderBleMux);
  const bool generationMatches = link.generation == grinderBleGeneration(arg);
  if (generationMatches && event->type == BLE_GAP_EVENT_DISCONNECT && link.handle == event->disconnect.conn.conn_handle) {
    link.reset();
  }
  const bool connecting = generationMatches && link.phase == GrinderBlePhase::Connecting;
  const bool closed = link.closeRequested;
  if (connecting && connectEvent) {
    if (!outgoingConnected) link.reset();
    else {
      link.handle = event->connect.conn_handle;
      grinderBleSetPhaseLocked(GrinderBlePhase::Service);
    }
  }
  portEXIT_CRITICAL(&grinderBleMux);

  if (!generationMatches) return 0;
  if (event->type == BLE_GAP_EVENT_NOTIFY_RX) grinderBleReceiveOnHost(event, arg);
  if (!connecting || !outgoingConnected) return 0;
  link.serviceStart = 0;
  link.serviceEnd = 0;
  link.commandHandle = 0;
  link.replyHandle = 0;
  link.replyEnd = 0;
  link.subscriptionHandle = 0;
  if (closed || ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &grinderBleServiceUuid.u,
                                         grinderBleServiceFound, arg) != 0) grinderBleCloseOnHost();
  return 0;
}

static bool grinderBleScanCandidate(const ble_gap_disc_desc &advertisement, GrinderDiscoveredPlug &candidate) {
  if (advertisement.event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) return false;
  ble_hs_adv_fields fields{};
  if (ble_hs_adv_parse_fields(&fields, advertisement.data, advertisement.length_data) != 0) return false;
  for (uint8_t i = 0; i < fields.num_uuids128; ++i) {
    if (ble_uuid_cmp(&fields.uuids128[i].u, &grinderBleServiceUuid.u) == 0) {
      return grinderBleAdvertisedMac(fields.mfg_data, fields.mfg_data_len, candidate.mac);
    }
  }
  return false;
}

static int grinderBleScanEvent(ble_gap_event *event, void *arg) {
  auto &link = grinderBleTransport;
  if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    portENTER_CRITICAL(&grinderBleMux);
    if (link.phase == GrinderBlePhase::Scanning && link.current(grinderBleGeneration(arg), grinderBleNoConnection)) {
      link.reset();
    }
    portEXIT_CRITICAL(&grinderBleMux);
    return 0;
  }
  if (!grinderBleCallbackCurrent(GrinderBlePhase::Scanning, grinderBleNoConnection, arg)) return 0;
  GrinderDiscoveredPlug candidate;
  if (event->type != BLE_GAP_EVENT_DISC || !grinderBleScanCandidate(event->disc, candidate)) return 0;

  portENTER_CRITICAL(&grinderBleMux);
  bool known = false;
  for (uint8_t i = 0; i < link.discoveryCount; ++i) {
    if (strcmp(link.discoveries[i].mac, candidate.mac) == 0) known = true;
  }
  if (!known && link.discoveryCount < grinderBleDiscoveryLimit) {
    link.discoveries[link.discoveryCount] = candidate;
    link.discoveryCount = link.discoveryCount + 1;
  }
  const bool target = !link.closeRequested && link.target[0] != '\0' && strcasecmp(link.target, candidate.mac) == 0;
  portEXIT_CRITICAL(&grinderBleMux);
  if (target && ble_gap_disc_cancel() == 0) {
    grinderBleAdvanceOnHost(GrinderBlePhase::Connecting);
    if (ble_gap_connect(link.ownAddressType, &event->disc.addr, grinderBlePhaseTimeoutMs,
                        nullptr, grinderBleLinkEvent, arg) != 0) grinderBleResetOnHost();
  }
  return 0;
}

static void grinderBleStartScanOnHost() {
  auto &link = grinderBleTransport;
  if (ble_hs_id_infer_auto(0, &link.ownAddressType) != 0) {
    grinderBleResetOnHost();
    return;
  }
  ble_gap_disc_params params{};
  params.passive = 1;
  params.filter_duplicates = 1;
  grinderBleAdvanceOnHost(GrinderBlePhase::Scanning);
  if (ble_gap_disc(link.ownAddressType, grinderBleScanMs, &params, grinderBleScanEvent,
                   grinderBleGenerationArg()) != 0) grinderBleResetOnHost();
}

static bool grinderBleSendOnHost(uint16_t handle, const GrinderBleCommand &command) {
  for (size_t offset = 0; offset < command.size; offset += grinderBleFragmentSize) {
    const size_t remaining = command.size - offset;
    const size_t count = remaining < grinderBleFragmentSize ? remaining : grinderBleFragmentSize;
    uint8_t packet[grinderBleFragmentSize + 1] = {command.id};
    memcpy(packet + 1, command.bytes + offset, count);
    if (ble_gattc_write_no_rsp_flat(handle, grinderBleTransport.commandHandle, packet, count + 1) != 0) return false;
  }
  return true;
}

static void grinderBleDriveOnHost(ble_npl_event *) {
  auto &link = grinderBleTransport;
  portENTER_CRITICAL(&grinderBleMux);
  link.wakePending = false;
  const GrinderBlePhase phase = link.phase;
  const bool close = link.closeRequested;
  const uint16_t handle = link.handle;
  const GrinderBleCommand command = link.takeCommand();
  portEXIT_CRITICAL(&grinderBleMux);

  if (close) {
    if (command.size != 0) grinderBleSendOnHost(handle, command);
    grinderBleCloseOnHost();
  } else if (phase == GrinderBlePhase::StartScan) {
    grinderBleStartScanOnHost();
  } else if (command.size != 0 && !grinderBleSendOnHost(handle, command)) {
    grinderBleCloseOnHost();
  }
}

static void grinderBleSchedule() {
  if (!grinderBleAvailable()) return;
  portENTER_CRITICAL(&grinderBleMux);
  const bool post = !grinderBleTransport.wakePending;
  grinderBleTransport.wakePending = true;
  portEXIT_CRITICAL(&grinderBleMux);
  if (!post) return;
  static ble_npl_event event{};
  if (event.event == nullptr) ble_npl_event_init(&event, grinderBleDriveOnHost, nullptr);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &event);
}

static bool grinderBleStartScan(const char *selectedMac) {
  if (!grinderBleAvailable() || (selectedMac != nullptr && strnlen(selectedMac, 18) != 17)) return false;
  auto &link = grinderBleTransport;
  portENTER_CRITICAL(&grinderBleMux);
  const bool idle = link.phase == GrinderBlePhase::Idle;
  if (idle) {
    link.generation = link.generation + 1;
    link.discoveryCount = 0;
    if (selectedMac == nullptr) link.target[0] = '\0';
    else memcpy(link.target, selectedMac, sizeof(link.target));
    grinderBleSetPhaseLocked(GrinderBlePhase::StartScan);
  }
  portEXIT_CRITICAL(&grinderBleMux);
  if (idle) grinderBleSchedule();
  return idle;
}

static bool grinderBleScanActive() {
  portENTER_CRITICAL(&grinderBleMux);
  const GrinderBlePhase phase = grinderBleTransport.phase;
  portEXIT_CRITICAL(&grinderBleMux);
  return phase == GrinderBlePhase::StartScan || phase == GrinderBlePhase::Scanning;
}

static bool grinderBleBusy() {
  portENTER_CRITICAL(&grinderBleMux);
  const bool busy = grinderBleTransport.phase != GrinderBlePhase::Idle && !grinderBleTransport.connected();
  portEXIT_CRITICAL(&grinderBleMux);
  return busy;
}

static bool grinderBleConnected() {
  if (!grinderBleAvailable()) return false;
  portENTER_CRITICAL(&grinderBleMux);
  const bool connected = grinderBleTransport.connected();
  portEXIT_CRITICAL(&grinderBleMux);
  return connected;
}

static void grinderBleRequestClose(bool scanOnly) {
  auto &link = grinderBleTransport;
  portENTER_CRITICAL(&grinderBleMux);
  const GrinderBlePhase phase = link.phase;
  const bool close = phase != GrinderBlePhase::Idle &&
                     (!scanOnly || phase == GrinderBlePhase::Scanning || phase == GrinderBlePhase::StartScan);
  if (close) link.closeRequested = true;
  portEXIT_CRITICAL(&grinderBleMux);
  if (close) grinderBleSchedule();
}

static void grinderBleStopScan() {
  grinderBleRequestClose(true);
}

static void grinderBleClose() {
  grinderBleRequestClose(false);
}

static size_t grinderBleCopyDiscoveries(GrinderDiscoveredPlug *out, size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  portENTER_CRITICAL(&grinderBleMux);
  const size_t count = grinderBleTransport.discoveryCount;
  const size_t toCopy = count < capacity ? count : capacity;
  memcpy(out, grinderBleTransport.discoveries, toCopy * sizeof(*out));
  portEXIT_CRITICAL(&grinderBleMux);
  return toCopy;
}

static bool grinderBleWrite(const char *data, size_t size, uint8_t requestId) {
  if (!grinderBleAvailable()) return false;
  portENTER_CRITICAL(&grinderBleMux);
  const bool queued = grinderBleTransport.queueCommand(data, size, requestId);
  portEXIT_CRITICAL(&grinderBleMux);
  if (queued) grinderBleSchedule();
  return queued;
}

static size_t grinderBleRead(uint8_t *requestId, char *data, size_t capacity) {
  if (!grinderBleAvailable()) return 0;
  portENTER_CRITICAL(&grinderBleMux);
  const size_t size = grinderBleTransport.readReply(requestId, data, capacity);
  portEXIT_CRITICAL(&grinderBleMux);
  return size;
}

static void grinderBleTick() {
  auto &link = grinderBleTransport;
  portENTER_CRITICAL(&grinderBleMux);
  const unsigned long now = millis();
  const bool busy = link.phase != GrinderBlePhase::Idle && !link.connected();
  if (busy && now - link.phaseAt >= grinderBlePhaseTimeoutMs) link.closeRequested = true;
  const bool closing = link.closeRequested && now - link.closeAt >= grinderBleCloseRetryMs;
  portEXIT_CRITICAL(&grinderBleMux);
  if (closing) grinderBleSchedule();
}

#endif
