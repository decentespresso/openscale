from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    reset = block_after((ROOT / "include/menu.h").read_text(encoding="utf-8"), "void resetWifi() {")
    save = block_after((ROOT / "src/wifi_setup.cpp").read_text(encoding="utf-8"),
                       "bool saveCredentials(const String &ssid, const String &pass) {")
    device = block_after((ROOT / "include/wifi_setup_routes.h").read_text(encoding="utf-8"),
                         "static bool wifiRequestDeviceAllowed(AsyncWebServerRequest *request) {")
    source = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
struct String : std::string {
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
  void toCharArray(char *out, size_t capacity) const {
    assert(size() < capacity);
    memcpy(out, c_str(), size() + 1);
  }
};
using portMUX_TYPE = int;
using StaticSemaphore_t = int;
using SemaphoreHandle_t = int *;
using TaskHandle_t = void *;
constexpr int portMAX_DELAY = 0, WL_CONNECTED = 3, WIFI_AP = 2, WIFI_STA = 1;
constexpr int WIFI_AP_STA = WIFI_AP | WIFI_STA;
constexpr int WIFI_SCAN_FAILED = -2, WIFI_SCAN_RUNNING = -1, WIFI_AUTH_OPEN = 0;
constexpr int WIFI_REASON_AUTH_FAIL = 1, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 2;
constexpr int WIFI_REASON_HANDSHAKE_TIMEOUT = 3, WIFI_REASON_NO_AP_FOUND = 4;
unsigned long clockMs = 1000;
unsigned long millis() { return clockMs; }
uint32_t esp_random() { return 13; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void *>(1); }
void portENTER_CRITICAL(int *mux) { assert(*mux == 0); *mux = 1; }
void portEXIT_CRITICAL(int *mux) { assert(*mux == 1); *mux = 0; }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(int *storage) { return storage; }
void xSemaphoreTake(int *, int) {}
void xSemaphoreGive(int *) {}
void esp_wifi_scan_stop() {}
struct { void println(const char *) {} } Serial;
#include "wifi_setup.h"
#include "wifi_settings.h"
std::map<String, String> strings;
std::map<String, std::vector<uint8_t>> blobs;
String failedRemoval;
bool pretendRemoval = false;
int failedWrites = 0;
struct Preferences {
  bool begin(const char *name, bool) { assert(strcmp(name, "wifi") == 0); return true; }
  bool isKey(const char *key) { return strings.count(key) || blobs.count(key); }
  size_t getBytesLength(const char *key) { return blobs.count(key) ? blobs.at(key).size() : 0; }
  size_t getBytes(const char *key, void *output, size_t capacity) {
    if (!blobs.count(key) || blobs.at(key).size() > capacity) return 0;
    const auto &data = blobs.at(key);
    memcpy(output, data.data(), data.size());
    return data.size();
  }
  size_t putBytes(const char *key, const void *input, size_t length) {
    if (failedWrites) { --failedWrites; return 0; }
    const auto *bytes = static_cast<const uint8_t *>(input);
    blobs[key] = std::vector<uint8_t>(bytes, bytes + length);
    return length;
  }
  String getString(const char *key, const char *fallback) {
    return strings.count(key) ? strings.at(key) : String(fallback);
  }
  size_t getString(const char *key, char *output, size_t capacity) {
    const String value = getString(key, "");
    value.toCharArray(output, capacity);
    return value.size();
  }
  size_t putString(const char *key, const char *value) { strings[key] = value; return strlen(value); }
  bool remove(const char *key) {
    if (failedRemoval == key) return pretendRemoval;
    return strings.erase(key) + blobs.erase(key) != 0;
  }
  void end() {}
};
struct Ip {
  uint32_t value = 0;
  operator uint32_t() const { return value; }
  String toString() const { return value ? "192.168.50.30" : "0.0.0.0"; }
};
struct Network { String ssid; int16_t rssi; bool secure; };
struct {
  int stationStatus = 0, currentMode = WIFI_AP;
  String stationSsid;
  Ip ip;
  std::vector<Network> networks;
  int status() const { return stationStatus; }
  int getMode() const { return currentMode; }
  void mode(int value) { currentMode = value; }
  Ip localIP() const { return ip; }
  Ip softAPIP() const { return Ip{1}; }
  String SSID() const { return stationSsid; }
  String SSID(int index) const { return networks.at(index).ssid; }
  int16_t RSSI(int index) const { return networks.at(index).rssi; }
  int encryptionType(int index) const { return networks.at(index).secure ? 1 : WIFI_AUTH_OPEN; }
  int scanNetworks(bool) { return WIFI_SCAN_RUNNING; }
  int scanComplete() const { return static_cast<int>(networks.size()); }
  void scanDelete() {}
} WiFi;
portMUX_TYPE wifiSetupMux = 0;
StaticSemaphore_t wifiRadioMutexStorage = 0, wifiSettingsMutexStorage = 0;
SemaphoreHandle_t wifiRadioMutex = nullptr, wifiSettingsMutex = nullptr;
volatile TaskHandle_t wifiWorkerTask = reinterpret_cast<void *>(1);
volatile bool wifiWorkerStarting = false, wifiWorkerStartRequested = false;
volatile bool wifiWorkerStopRequested = false, wifiOperationReserved = false;
volatile bool b_wifiEnabled = true, b_ota = false;
volatile WifiSetupRequest wifiPendingRequest;
volatile WifiSetupStatus wifiSetupStatus;
volatile WifiScanResult wifiScanResult;
volatile uint32_t wifiGotIpGeneration = 0, wifiDisconnectGeneration = 0;
volatile uint16_t wifiDisconnectReason = WIFI_REASON_NO_AP_FOUND;
volatile bool wifiStaAssociated = false;
WifiSetupRuntime wifiSetupRuntime;
const char *wifiPrefsKey = "wifi", *wifiSSIDKey = "ssid", *wifiPassKey = "pass";
const char *wifiMdnsNameKey = "mdns_name";
WiFiParams params;
int stationStarts = 0, apStarts = 0;
void wifiStartStation(const char *ssid, const char *) {
  ++stationStarts;
  wifiSetupRuntime.ipBaseline = wifiGotIpGeneration;
  WiFi.stationSsid = ssid;
  WiFi.stationStatus = 0;
  WiFi.ip.value = 0;
  WiFi.currentMode = WIFI_STA;
}
void setupAP() { ++apStarts; WiFi.currentMode = WIFI_AP; WiFi.stationStatus = 0; clockMs += 100; }
#include "wifi_settings.cpp"
#include "wifi_setup_control.cpp"
#include "wifi_scan.cpp"
bool saveCredentials(const String &ssid, const String &pass) { @SAVE@ }
String actionMessage, actionMessage2;
int t_actionMessageDelay = 0, restartRequests = 0, menuMessages = 0;
void menuActionMessageChanged() { ++menuMessages; }
void markMenuRestartRequired() { ++restartRequests; }
void resetWifi() { @RESET@ }
String wifiSetupDeviceId() { return "111111111111"; }
struct Header {
  String text;
  String value() const { return text; }
};
struct AsyncWebServerRequest {
  std::map<String, Header> headers;
  int responseCode = 0;
  String responseBody;
  bool hasHeader(const char *name) const { return headers.count(name) != 0; }
  const Header *getHeader(const char *name) const { return &headers.at(name); }
  void send(int code, const char *, const char *body) { responseCode = code; responseBody = body; }
};
static bool wifiRequestDeviceAllowed(AsyncWebServerRequest *request) { @DEVICE@ }

void testDeviceMutationGuard() {
  AsyncWebServerRequest request;
  assert(wifiRequestDeviceAllowed(&request));
  request.headers["X-HDS-Device-ID"] = Header{"111111111111"};
  assert(wifiRequestDeviceAllowed(&request) && request.responseCode == 0);
  for (const char *value : {"222222222222", "", "11111111111", "111111111111 "}) {
    request.headers["X-HDS-Device-ID"] = Header{value};
    assert(!wifiRequestDeviceAllowed(&request) && request.responseCode == 409);
    assert(request.responseBody == "{\"error\":\"wifi_device_mismatch\"}");
  }
}

void seedCredentials(bool legacyOnly) {
  strings = {{"ssid", "Old network"}, {"pass", "old pass"}, {"mdns_name", "scale"}};
  blobs.clear();
  failedRemoval.clear();
  pretendRemoval = false;
  failedWrites = 0;
  params = WiFiParams{};
  params.init();
  if (!legacyOnly) assert(params.saveCredentials("Old network", "old pass"));
}

void assertPreviousCredentials() {
  assert(params.getSSID() == "Old network" && params.getPass() == "old pass");
  WiFiParams reloaded;
  reloaded.init();
  assert(reloaded.getSSID() == "Old network" && reloaded.getPass() == "old pass");
  assert(strings.at("mdns_name") == "scale");
}

void testCredentialResetFailures() {
  for (const bool legacyOnly : {false, true}) {
    for (const char *key : {"ssid", "pass"}) {
      for (const bool pretend : {false, true}) {
        seedCredentials(legacyOnly);
        failedRemoval = key;
        pretendRemoval = pretend;
        assert(!params.saveCredentials("", ""));
        assert(strings.count(key) == 1);
        assertPreviousCredentials();
      }
    }
    seedCredentials(legacyOnly);
    assert(params.saveCredentials("", ""));
    assert(!params.hasCredentials() && !strings.count("ssid") && !strings.count("pass"));
    assert(strings.at("mdns_name") == "scale");
    WiFiParams reloaded;
    reloaded.init();
    assert(!reloaded.hasCredentials());
    assert(params.saveCredentials("", ""));
  }
  seedCredentials(false);
  failedWrites = 1;
  assert(!params.saveCredentials("New network", "new pass"));
  assertPreviousCredentials();
  seedCredentials(false);
  failedRemoval = "pass";
  resetWifi();
  assert(actionMessage == "WiFi Reset Failed" && actionMessage2 == "Storage error");
  assert(restartRequests == 0 && menuMessages == 1);
  assertPreviousCredentials();
  failedRemoval.clear();
  resetWifi();
  assert(actionMessage == "WiFi Reset" && restartRequests == 1 && menuMessages == 2);
}

void testScannedSsidVerification() {
  seedCredentials(false);
  WiFi.currentMode = WIFI_AP;
  WiFi.networks = {{" Cafe ", -45, true}, {" Cafe ", -70, true}, {"Bad\t", -30, true}};
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Scan, {}, id));
  assert(!wifiScannedSsid(" Cafe ", 6));
  wifiProcessSetup();
  assert(wifiReadScanResult().count == 1);
  assert(wifiScannedSsid(" Cafe ", 6));
  assert(!wifiScannedSsid("Cafe", 4));
  assert(!wifiScannedSsid("Unknown ", 8));
  assert(!wifiScannedSsid(nullptr, 6));
  assert(!wifiScannedSsid("", 0));
  assert(!wifiScannedSsid(" Cafe ", 33));
  const char embedded[] = {' ', 'C', 'a', 0, 'e', ' '};
  assert(!wifiScannedSsid(embedded, sizeof(embedded)));
  assert(wifiQueueSetup(WifiSetupCommand::Scan, {}, id));
  assert(!wifiScannedSsid(" Cafe ", 6));
  wifiProcessSetup();
}

void testFailedOledResetPreservesActiveSwitch() {
  seedCredentials(false);
  wifiSetupRuntime = {};
  restartRequests = 0;
  WifiCredentials candidate;
  strcpy(candidate.ssid, "New network");
  strcpy(candidate.pass, "new pass");
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Switch, candidate, id));
  wifiProcessSetup();
  clockMs += 500;
  wifiProcessSetup();
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.ip.value = 1;
  wifiGotIpGeneration = wifiGotIpGeneration + 1;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Verifying);
  const unsigned long verificationAt = wifiSetupRuntime.change.startedAt;
  failedWrites = 1;
  resetWifi();
  assert(actionMessage == "WiFi Reset Failed" && restartRequests == 0);
  assert(wifiSetupBusy() && wifiSetupRuntime.change.phase == WifiSwitchPhase::Verifying);
  assert(wifiSetupRuntime.operationId == id && wifiSetupRuntime.change.startedAt == verificationAt);
  assert(strcmp(wifiSetupRuntime.candidate.ssid, "New network") == 0);
  assertPreviousCredentials();
  clockMs += 3000;
  wifiProcessSetup();
  assert(!wifiSetupBusy() && wifiSetupRuntime.change.phase == WifiSwitchPhase::Succeeded);
  assert(params.getSSID() == "New network" && WiFi.stationSsid == "New network");
  WiFiParams reloaded;
  reloaded.init();
  assert(reloaded.getSSID() == "New network");
  resetWifi();
  assert(actionMessage == "WiFi Reset" && restartRequests == 1);
  assert(!params.hasCredentials());
}

void testRepeatedUnavailableSavedNetworkRecovery() {
  seedCredentials(false);
  wifiSetupRuntime = {};
  WiFi.currentMode = WIFI_STA;
  const auto previous = blobs.at("credentials");
  WifiCredentials candidate;
  strcpy(candidate.ssid, "Missing network");
  strcpy(candidate.pass, "password");
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Switch, candidate, id));
  wifiProcessSetup();
  clockMs += 500;
  wifiProcessSetup();
  clockMs += 20000;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
  clockMs += 20000;
  wifiProcessSetup();
  assert(WiFi.currentMode == WIFI_AP && !wifiSetupBusy());
  for (int cycle = 0; cycle < 2; ++cycle) {
    const unsigned long openedAt = wifiSetupRuntime.recoveryApAt;
    clockMs = openedAt + 599999;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_AP);
    assert(wifiReserveExternalOperation());
    clockMs = openedAt + 600000;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_AP);
    wifiReleaseExternalOperation();
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_STA && wifiSetupBusy());
    assert(WiFi.stationSsid == "Old network");
    assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
    assert(!wifiQueueSetup(WifiSetupCommand::Switch, candidate, id));
    clockMs += 19999;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_STA);
    clockMs += 1;
    wifiProcessSetup();
    assert(WiFi.currentMode == WIFI_AP && !wifiSetupBusy());
    assert(wifiSetupRuntime.recoveryApAt == clockMs);
    assert(blobs.at("credentials") == previous);
  }
  clockMs += 600000;
  wifiProcessSetup();
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.ip.value = 1;
  wifiProcessSetup();
  assert(wifiSetupBusy());
  wifiGotIpGeneration = wifiGotIpGeneration + 1;
  wifiProcessSetup();
  assert(!wifiSetupBusy() && WiFi.currentMode == WIFI_STA);
  assert(wifiSetupRuntime.recoveryApAt == 0 && blobs.at("credentials") == previous);
  assert(apStarts == 3 && stationStarts == 5);
}

void testFailedHttpResetKeepsRecoveryTimer() {
  seedCredentials(false);
  wifiSetupRuntime = {};
  wifiSetupRuntime.change.phase = WifiSwitchPhase::Failed;
  wifiSetupRuntime.recoveryApAt = clockMs;
  WiFi.currentMode = WIFI_AP;
  const unsigned long recoveryAt = clockMs;
  const int before = stationStarts;
  failedWrites = 1;
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Reset, {}, id));
  clockMs += 500;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.error == WifiSetupError::Storage);
  assert(wifiSetupRuntime.recoveryApAt == recoveryAt);
  assertPreviousCredentials();
  clockMs = recoveryAt + 600000;
  wifiProcessSetup();
  assert(stationStarts == before + 1 && WiFi.currentMode == WIFI_STA);
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
  clockMs += 20000;
  wifiProcessSetup();
  assert(!wifiSetupBusy() && WiFi.currentMode == WIFI_AP);
  assert(wifiQueueSetup(WifiSetupCommand::Reset, {}, id));
  clockMs += 500;
  wifiProcessSetup();
  assert(!params.hasCredentials() && wifiSetupRuntime.recoveryApAt == 0);
}

void testDhcpRecoveryInvalidatesStability() {
  seedCredentials(false);
  wifiSetupRuntime = {};
  WifiCredentials candidate;
  strcpy(candidate.ssid, "New network");
  strcpy(candidate.pass, "new pass");
  uint32_t id;
  assert(wifiQueueSetup(WifiSetupCommand::Switch, candidate, id));
  wifiProcessSetup();
  clockMs += 500;
  wifiProcessSetup();
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.ip.value = 1;
  wifiGotIpGeneration = wifiGotIpGeneration + 1;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Verifying);
  clockMs += 2999;
  WiFi.stationStatus = 0;
  WiFi.ip.value = 0;
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.ip.value = 2;
  wifiGotIpGeneration = wifiGotIpGeneration + 1;
  clockMs += 1;
  wifiProcessSetup();
  assert(wifiSetupRuntime.change.phase == WifiSwitchPhase::Restoring);
  assertPreviousCredentials();
  clockMs += 20000;
  wifiProcessSetup();
  assert(!wifiSetupBusy() && WiFi.currentMode == WIFI_AP);
}

int main() {
  testDeviceMutationGuard();
  testCredentialResetFailures();
  testScannedSsidVerification();
  testRepeatedUnavailableSavedNetworkRecovery();
  testFailedOledResetPreservesActiveSwitch();
  testFailedHttpResetKeepsRecoveryTimer();
  testDhcpRecoveryInvalidatesStability();
}
'''.replace("@RESET@", reset).replace("@SAVE@", save).replace("@DEVICE@", device)
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for name in ("Arduino.h", "Preferences.h", "WiFi.h", "esp_random.h", "esp_wifi.h"):
            (root / name).write_text("", encoding="utf-8")
        (root / "hds_features.h").write_text("#define HDS_FEATURE_WIFI 1\n", encoding="utf-8")
        cpp, binary = root / "wifi.cpp", root / "wifi.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                        "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("WiFi AP retry, scan verification, credential persistence, and OLED reset runtime tests passed")


if __name__ == "__main__":
    main()
