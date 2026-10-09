#include "hds_features.h"
#if HDS_FEATURE_WIFI
#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>
#include "wifi_setup.h"

void wifiBeginScan() {
  wifiSetupRuntime.scanPreviousMode = WiFi.getMode();
  wifiSetupRuntime.scanStartedAt = millis();
  if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true);
  if (result == WIFI_SCAN_FAILED) wifiSetupRuntime.scanStartedAt = 0;
}

void wifiProcessScan() {
  portENTER_CRITICAL(&wifiSetupMux);
  const bool scanning = wifiScanResult.state == WifiScanState::Running;
  portEXIT_CRITICAL(&wifiSetupMux);
  if (!scanning) return;
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING && wifiSetupRuntime.scanStartedAt != 0 &&
      millis() - wifiSetupRuntime.scanStartedAt < 10000) return;
  WifiScanResult result;
  result.state = count >= 0 ? WifiScanState::Complete : WifiScanState::Failed;
  for (int index = 0; index < count; ++index) {
    const String ssid = WiFi.SSID(index);
    char checked[33];
    if (ssid.length() == 0 || ssid.length() > 32 ||
        !wifiNormalizeField(ssid.c_str(), ssid.length(), checked, sizeof(checked))) continue;
    WifiScanNetwork network;
    ssid.toCharArray(network.ssid, sizeof(network.ssid));
    network.rssi = WiFi.RSSI(index);
    network.secure = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    size_t slot = 0;
    while (slot < result.count && strcmp(result.networks[slot].ssid, network.ssid) != 0) ++slot;
    if (slot < result.count) {
      if (network.rssi > result.networks[slot].rssi) result.networks[slot] = network;
    } else if (result.count < 20) {
      result.networks[result.count++] = network;
    } else {
      const auto weakest = std::min_element(result.networks, result.networks + result.count,
          [](const WifiScanNetwork &left, const WifiScanNetwork &right) {
            return left.rssi < right.rssi;
          });
      if (network.rssi > weakest->rssi) *weakest = network;
    }
  }
  std::sort(result.networks, result.networks + result.count,
      [](const WifiScanNetwork &left, const WifiScanNetwork &right) {
        return left.rssi > right.rssi;
      });
  if (count == WIFI_SCAN_RUNNING) esp_wifi_scan_stop();
  WiFi.scanDelete();
  if (wifiSetupRuntime.scanPreviousMode == WIFI_AP) WiFi.mode(WIFI_AP);
  portENTER_CRITICAL(&wifiSetupMux);
  memcpy((void *)&wifiScanResult, &result, sizeof(result));
  wifiOperationReserved = false;
  portEXIT_CRITICAL(&wifiSetupMux);
  wifiSetupRuntime.scanStartedAt = 0;
}
#endif
