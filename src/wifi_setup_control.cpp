#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include "wifi_setup.h"
#include "wifi_settings.h"

void wifiProcessScan();
void wifiBeginScan();
extern volatile bool b_ota;

void wifiInitLocks() {
  portENTER_CRITICAL(&wifiSetupMux);
  if (wifiRadioMutex == nullptr) {
    wifiRadioMutex = xSemaphoreCreateMutexStatic(&wifiRadioMutexStorage);
    wifiSettingsMutex = xSemaphoreCreateMutexStatic(&wifiSettingsMutexStorage);
  }
  portEXIT_CRITICAL(&wifiSetupMux);
}

bool wifiRequestWorkerStart() {
  wifiInitLocks();
  portENTER_CRITICAL(&wifiSetupMux);
  wifiWorkerStartRequested = true;
  wifiWorkerStopRequested = false;
  const bool create = wifiWorkerTask == nullptr && !wifiWorkerStarting;
  if (create) wifiWorkerStarting = true;
  portEXIT_CRITICAL(&wifiSetupMux);
  return create;
}

void wifiWorkerStarted() {
  portENTER_CRITICAL(&wifiSetupMux);
  wifiWorkerTask = xTaskGetCurrentTaskHandle();
  wifiWorkerStarting = false;
  wifiWorkerStartRequested = false;
  portEXIT_CRITICAL(&wifiSetupMux);
}

void wifiWorkerStartFailed() {
  portENTER_CRITICAL(&wifiSetupMux);
  wifiWorkerStarting = false;
  portEXIT_CRITICAL(&wifiSetupMux);
}

bool wifiWorkerNeedsStart() {
  portENTER_CRITICAL(&wifiSetupMux);
  const bool start = wifiWorkerStartRequested && !wifiWorkerStopRequested;
  wifiWorkerStartRequested = false;
  portEXIT_CRITICAL(&wifiSetupMux);
  return start;
}

WifiSetupStatus wifiReadSetupStatus() {
  WifiSetupStatus status;
  portENTER_CRITICAL(&wifiSetupMux);
  memcpy(&status, (const void *)&wifiSetupStatus, sizeof(status));
  portEXIT_CRITICAL(&wifiSetupMux);
  return status;
}

WifiScanResult wifiReadScanResult() {
  WifiScanResult result;
  portENTER_CRITICAL(&wifiSetupMux);
  memcpy(&result, (const void *)&wifiScanResult, sizeof(result));
  portEXIT_CRITICAL(&wifiSetupMux);
  return result;
}

bool wifiScannedSsid(const char *ssid, size_t length) {
  if (ssid == nullptr || length == 0 || length > 32) return false;
  const WifiScanResult scan = wifiReadScanResult();
  if (scan.state != WifiScanState::Complete) return false;
  for (uint8_t index = 0; index < scan.count; ++index) {
    if (strlen(scan.networks[index].ssid) == length &&
        memcmp(scan.networks[index].ssid, ssid, length) == 0) return true;
  }
  return false;
}

bool wifiSetupBusy() {
  portENTER_CRITICAL(&wifiSetupMux);
  const bool busy = wifiOperationReserved;
  portEXIT_CRITICAL(&wifiSetupMux);
  return busy;
}

bool wifiReserveExternalOperation() {
  portENTER_CRITICAL(&wifiSetupMux);
  const bool accepted = !wifiOperationReserved && !b_ota;
  if (accepted) wifiOperationReserved = true;
  portEXIT_CRITICAL(&wifiSetupMux);
  return accepted;
}

void wifiReleaseExternalOperation() {
  portENTER_CRITICAL(&wifiSetupMux);
  wifiOperationReserved = false;
  portEXIT_CRITICAL(&wifiSetupMux);
}

bool wifiQueueSetup(WifiSetupCommand command, const WifiCredentials &credentials,
                   uint32_t &operationId) {
  const uint32_t id = esp_random() | 1u;
  const WifiSetupRequest request = {command, credentials, id, millis()};
  portENTER_CRITICAL(&wifiSetupMux);
  const bool accepted = !wifiOperationReserved && !b_ota && !wifiWorkerStopRequested &&
                        b_wifiEnabled && wifiWorkerTask != nullptr;
  if (accepted) {
    wifiOperationReserved = true;
    memcpy((void *)&wifiPendingRequest, &request, sizeof(request));
    wifiSetupStatus.operationId = id;
    wifiSetupStatus.phase = WifiSwitchPhase::Queued;
    wifiSetupStatus.error = WifiSetupError::None;
    memcpy((void *)wifiSetupStatus.requestedSsid, credentials.ssid, sizeof(credentials.ssid));
    if (command == WifiSetupCommand::Scan) wifiScanResult.state = WifiScanState::Running;
    operationId = id;
  }
  portEXIT_CRITICAL(&wifiSetupMux);
  return accepted;
}

void wifiPublishSetupStatus() {
  const bool connected = WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0;
  const bool accessPoint = (WiFi.getMode() & WIFI_AP) != 0;
  const String ssid = connected ? WiFi.SSID() : params.getSSID();
  const String ip = accessPoint ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  WifiSetupStatus status;
  status.phase = wifiSetupRuntime.change.phase;
  status.error = wifiSetupRuntime.change.error;
  status.operationId = wifiSetupRuntime.operationId;
  memcpy(status.requestedSsid, wifiSetupRuntime.requestedSsid, sizeof(status.requestedSsid));
  ssid.toCharArray(status.ssid, sizeof(status.ssid));
  ip.toCharArray(status.ip, sizeof(status.ip));
  status.connected = connected;
  status.accessPoint = accessPoint;
  status.credentialsSaved = params.hasCredentials();
  portENTER_CRITICAL(&wifiSetupMux);
  if (wifiPendingRequest.command != WifiSetupCommand::None) {
    status.phase = wifiSetupStatus.phase;
    status.error = wifiSetupStatus.error;
    status.operationId = wifiSetupStatus.operationId;
    memcpy(status.requestedSsid, (const void *)wifiSetupStatus.requestedSsid,
           sizeof(status.requestedSsid));
  }
  memcpy((void *)&wifiSetupStatus, &status, sizeof(status));
  portEXIT_CRITICAL(&wifiSetupMux);
}

void wifiCancelSetup() {
  wifiSetupRuntime.change = {};
  wifiSetupRuntime.change.phase = WifiSwitchPhase::Failed;
  wifiSetupRuntime.change.error = WifiSetupError::Aborted;
  wifiSetupRuntime.candidate = {};
  wifiSetupRuntime.recoveryApAt = 0;
  const WifiSetupRequest empty;
  portENTER_CRITICAL(&wifiSetupMux);
  const bool scanning = wifiScanResult.state == WifiScanState::Running;
  memcpy((void *)&wifiPendingRequest, &empty, sizeof(empty));
  wifiScanResult.state = WifiScanState::Idle;
  wifiOperationReserved = false;
  portEXIT_CRITICAL(&wifiSetupMux);
  if (scanning) esp_wifi_scan_stop();
  WiFi.scanDelete();
}

static WifiSetupError wifiConnectionFailure() {
  portENTER_CRITICAL(&wifiSetupMux);
  const uint16_t reason = wifiDisconnectReason;
  const bool associated = wifiStaAssociated;
  portEXIT_CRITICAL(&wifiSetupMux);
  if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
      reason == WIFI_REASON_HANDSHAKE_TIMEOUT) return WifiSetupError::Authentication;
  if (reason == WIFI_REASON_NO_AP_FOUND) return WifiSetupError::NotFound;
  return associated ? WifiSetupError::Dhcp : WifiSetupError::Timeout;
}

static bool wifiFreshConnection(const char *ssid, uint32_t &connectionGeneration) {
  portENTER_CRITICAL(&wifiSetupMux);
  const bool freshIp = wifiGotIpGeneration != wifiSetupRuntime.ipBaseline;
  connectionGeneration = wifiDisconnectGeneration;
  portEXIT_CRITICAL(&wifiSetupMux);
  return freshIp && WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0 &&
         WiFi.SSID() == ssid;
}

static void wifiExecuteSwitchAction(WifiSwitchAction action) {
  if (action == WifiSwitchAction::Connect) {
    wifiStartStation(wifiSetupRuntime.candidate.ssid, wifiSetupRuntime.candidate.pass);
    wifiSetupRuntime.change.startedAt = millis();
  } else if (action == WifiSwitchAction::Restore) {
    wifiStartStation(params.getSSID().c_str(), params.getPass().c_str());
    wifiSetupRuntime.change.startedAt = millis();
  } else if (action == WifiSwitchAction::AccessPoint) {
    setupAP();
    wifiSetupRuntime.recoveryApAt = millis();
  }
}

static void wifiFinishSetup() {
  wifiSetupRuntime.candidate = {};
  wifiPublishSetupStatus();
  wifiReleaseExternalOperation();
}

void wifiProcessSetup() {
  const unsigned long now = millis();
  WifiSetupRequest request;
  portENTER_CRITICAL(&wifiSetupMux);
  memcpy(&request, (const void *)&wifiPendingRequest, sizeof(request));
  if (request.command != WifiSetupCommand::None &&
      (request.command != WifiSetupCommand::Reset || now - request.queuedAt >= 500)) {
    const WifiSetupRequest empty;
    memcpy((void *)&wifiPendingRequest, &empty, sizeof(empty));
  } else {
    request.command = WifiSetupCommand::None;
  }
  portEXIT_CRITICAL(&wifiSetupMux);
  if (request.command != WifiSetupCommand::None) {
    wifiSetupRuntime.operationId = request.operationId;
    memcpy(wifiSetupRuntime.requestedSsid, request.credentials.ssid,
           sizeof(wifiSetupRuntime.requestedSsid));
    if (request.command != WifiSetupCommand::Scan) wifiSetupRuntime.recoveryApAt = 0;
    wifiSetupRuntime.change = {};
    if (request.command == WifiSetupCommand::Scan) {
      wifiBeginScan();
    } else if (request.command == WifiSetupCommand::Reset) {
      const bool saved = params.saveCredentials("", "");
      wifiSetupRuntime.change.phase = saved ? WifiSwitchPhase::Succeeded : WifiSwitchPhase::Failed;
      wifiSetupRuntime.change.error = saved ? WifiSetupError::None : WifiSetupError::Storage;
      if (saved) setupAP();
      wifiFinishSetup();
    } else {
      wifiSetupRuntime.candidate = request.credentials;
      wifiSetupRuntime.change.phase = WifiSwitchPhase::Queued;
      wifiSetupRuntime.change.startedAt = request.queuedAt;
      wifiSetupRuntime.change.hasPrevious = params.hasCredentials();
    }
  }
  wifiProcessScan();
  if (wifiSetupRuntime.change.busy()) {
    const char *expected = wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring
                               ? params.getSSID().c_str() : wifiSetupRuntime.candidate.ssid;
    uint32_t connectionGeneration;
    const bool connected = wifiFreshConnection(expected, connectionGeneration);
    WifiSwitchAction action = wifiSetupRuntime.change.tick(
        now, connected, wifiConnectionFailure(), connectionGeneration);
    if (action == WifiSwitchAction::Save) {
      const bool saved = params.saveCredentials(wifiSetupRuntime.candidate.ssid,
                                               wifiSetupRuntime.candidate.pass);
      action = wifiSetupRuntime.change.saved(now, saved);
    }
    wifiExecuteSwitchAction(action);
    if (!wifiSetupRuntime.change.busy()) wifiFinishSetup();
  }
  if (wifiSetupRuntime.recoveryApAt != 0 && params.hasCredentials() &&
      !wifiSetupBusy() && millis() - wifiSetupRuntime.recoveryApAt >= 600000 &&
      wifiReserveExternalOperation()) {
    wifiSetupRuntime.recoveryApAt = 0;
    wifiSetupRuntime.change.hasPrevious = true;
    wifiExecuteSwitchAction(wifiSetupRuntime.change.restore(now, wifiSetupRuntime.change.error));
  }
}

const char *wifiSetupPhaseName(WifiSwitchPhase phase) {
  switch (phase) {
    case WifiSwitchPhase::Queued: return "queued";
    case WifiSwitchPhase::Connecting: return "testing";
    case WifiSwitchPhase::Verifying: return "verifying";
    case WifiSwitchPhase::Saving: return "saving";
    case WifiSwitchPhase::Restoring: return "restoring";
    case WifiSwitchPhase::Succeeded: return "succeeded";
    case WifiSwitchPhase::Failed: return "failed";
    default: return "idle";
  }
}

const char *wifiSetupErrorName(WifiSetupError error) {
  switch (error) {
    case WifiSetupError::Authentication: return "authentication_failed";
    case WifiSetupError::NotFound: return "network_not_found";
    case WifiSetupError::Dhcp: return "dhcp_timeout";
    case WifiSetupError::Timeout: return "connection_timeout";
    case WifiSetupError::Storage: return "wifi_credentials_save_failed";
    case WifiSetupError::Aborted: return "aborted";
    case WifiSetupError::Scan: return "scan_failed";
    default: return "";
  }
}
#endif
