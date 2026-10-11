from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    wifi = (ROOT / "src/wifi_setup.cpp").read_text(encoding="utf-8")
    control = (ROOT / "src/wifi_setup_control.cpp").read_text(encoding="utf-8")
    custom = (ROOT / "include/custom_build_ota.h").read_text(encoding="utf-8")
    ota = (ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
    constants = "\n".join(re.findall(
        r"static const unsigned long HDS_OTA_(?:PENDING_)?WIFI_TIMEOUT_MS = [^;]+;", ota))
    source = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <string>
using String = std::string;
using BaseType_t = int;
constexpr int WL_CONNECTED = 3, WIFI_STA = 1, WIFI_AP = 2, portMAX_DELAY = 0, pdPASS = 1;
constexpr unsigned long WIFI_SUPERVISE_INTERVAL_MS = 250, HDS_OTA_TASK_STACK_BYTES = 24576;
constexpr unsigned long OTA_RUNTIME_PAUSE_TIMEOUT_MS = 1000;
constexpr uint32_t WSP_OTA_RESET = 1;
unsigned long clockMs = 1000, connectAt = 0;
unsigned long millis() { return clockMs; }
void *wifiWorkerTask = reinterpret_cast<void *>(1);
void *currentTask = wifiWorkerTask;
void *xTaskGetCurrentTaskHandle() { return currentTask; }
bool b_wifiEnabled = true, b_wifiOnBoot = true, b_ota = false, b_pullOtaRunning = false;
bool g_wifiInitDone = true, deviceConnected = false, wifiOperationReserved = false;
uint32_t g_wifiDisconnects = 0, g_wifiReconnects = 0, wsPendingMask = 0;
int wifiRadioMutex = 0, wifiSetupMux = 0, wsPendingMux = 0, radioDepth = 0;
int reconnects = 0, setupCalls = 0, startedTasks = 0, deletedTasks = 0;
int customRuns = 0, pairings = 0, resumes = 0;
bool taskCreateOk = true, pauseReady = true, relinkConfirmed = true;
bool lastInteractive = false;
String lastSsid, lastPass, message;
std::atomic<bool> filesystemRecoveryActive{false};
struct { unsigned long stationStartedAt = 0; } wifiSetupRuntime;
struct Params {
  bool saved = true;
  bool hasCredentials() { return saved; }
  String getSSID() { return "saved-network"; }
  String getPass() { return "saved-password"; }
} params;
struct {
  int stationStatus = 0, mode = WIFI_STA;
  int status() { return stationStatus; }
  int getMode() { return mode; }
  int RSSI() { return -50; }
} WiFi;
struct {
  uint32_t heap = 200000;
  uint32_t getFreeHeap() { return heap; }
  uint32_t getMinFreeHeap() { return heap; }
} ESP;
struct {
  template<class... T> void printf(const char *, T...) {}
  void flush() {}
} Serial;
void portENTER_CRITICAL(int *mux) { assert(*mux == 0); *mux = 1; }
void portEXIT_CRITICAL(int *mux) { assert(*mux == 1); *mux = 0; }
void xSemaphoreTake(int, int) { assert(radioDepth == 0); ++radioDepth; }
void xSemaphoreGive(int) { assert(radioDepth == 1); --radioDepth; }
void esp_restart() { assert(false); }
bool hdsIntervalElapsed(unsigned long now, unsigned long before, unsigned long interval) {
  return now - before >= interval;
}
void wifiProcessSetup() { ++setupCalls; }
void wifiPublishSetupStatus() {}
bool wifiSetupBusy() { return wifiOperationReserved; }
void wifiStartStation(const char *ssid, const char *pass) {
  assert(currentTask == wifiWorkerTask && radioDepth == 1);
  ++reconnects;
  lastSsid = ssid;
  lastPass = pass;
  wifiSetupRuntime.stationStartedAt = clockMs;
}
void setOtaRuntimePaused(bool) {}
bool otaRuntimeIsPaused() { return pauseReady; }
bool pullOtaFail(const char *value, const char * = "") { message = value; return false; }
void pullOtaDraw(const char *, const char * = "") {}
void pullOtaResumeFilesystemServices() { ++resumes; }
void customBuildRun(bool interactive) { ++customRuns; lastInteractive = interactive; }
bool customBuildConfirmRelink() { return relinkConfirmed; }
bool customBuildPairScale(bool interactive) { assert(!interactive); ++pairings; return true; }
void vTaskDelete(void *) { ++deletedTasks; }
int xTaskCreate(void (*)(void *), const char *, unsigned long, void *, int, void *) {
  assert(wifiOperationReserved && b_ota && b_pullOtaRunning);
  ++startedTasks;
  return taskCreateOk ? pdPASS : 0;
}
void wifiSupervise();
void delay(unsigned long interval) {
  clockMs += interval;
  if (connectAt && clockMs >= connectAt) WiFi.stationStatus = WL_CONNECTED;
  void *previous = currentTask;
  currentTask = wifiWorkerTask;
  wifiSupervise();
  currentTask = previous;
}
bool wifiCredentialsSaved() { return params.hasCredentials(); }
void wifi_init() { b_wifiEnabled = true; wifiSetupRuntime.stationStartedAt = clockMs; }
@CONSTANTS@
'''.replace("@CONSTANTS@", constants)
    for text, declaration in (
        (control, "bool wifiReserveExternalOperation()"),
        (control, "void wifiReleaseExternalOperation()"),
        (wifi, "static void wifiReconnectSavedNetwork(unsigned long now)"),
        (wifi, "void wifiSupervise()"),
        (ota, "bool pullOtaEnsureWifi(unsigned long timeoutMs = HDS_OTA_WIFI_TIMEOUT_MS)"),
        (custom, "void customBuildTask(void *args)"),
        (custom, "void customBuildStart(bool relink, bool interactive = true)"),
    ):
        source += declaration + " {" + block_after(text, declaration + " {") + "}\n"
    source += r'''
void resetConnection() {
  b_ota = b_pullOtaRunning = wifiOperationReserved = false;
  b_wifiEnabled = g_wifiInitDone = params.saved = true;
  currentTask = wifiWorkerTask;
  ESP.heap = 200000;
  clockMs += 100000;
  WiFi.stationStatus = WL_CONNECTED;
  WiFi.mode = WIFI_STA;
  wifiSupervise();
  reconnects = setupCalls = 0;
  WiFi.stationStatus = 0;
  wifiSetupRuntime.stationStartedAt = clockMs;
  connectAt = 0;
}
void testOtaReconnect() {
  resetConnection();
  assert(wifiReserveExternalOperation());
  b_ota = true;
  ESP.heap = 1000;
  clockMs += 19000;
  wifiSupervise();
  assert(reconnects == 0);
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 1 && setupCalls == 0);
  assert(lastSsid == params.getSSID() && lastPass == params.getPass());
  clockMs += 9999;
  wifiSupervise();
  assert(reconnects == 1);
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 2 && setupCalls == 0 && radioDepth == 0);
  WiFi.stationStatus = WL_CONNECTED;
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 2 && setupCalls == 0);
}
void testReconnectExclusions() {
  resetConnection();
  wifiOperationReserved = true;
  clockMs += 30000;
  wifiSupervise();
  assert(reconnects == 0);
  wifiOperationReserved = false;
  currentTask = reinterpret_cast<void *>(2);
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 0);
  currentTask = wifiWorkerTask;
  for (const int mode : {WIFI_AP, WIFI_AP | WIFI_STA}) {
    WiFi.mode = mode;
    clockMs += 1000;
    wifiSupervise();
    assert(reconnects == 0);
  }
  WiFi.mode = WIFI_STA;
  params.saved = false;
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 0);
  params.saved = true;
  b_wifiEnabled = false;
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 0);
  b_wifiEnabled = true;
  clockMs += 1000;
  wifiSupervise();
  assert(reconnects == 1);
}
void testPendingWifiStartup() {
  resetConnection();
  assert(wifiReserveExternalOperation());
  b_ota = true;
  currentTask = reinterpret_cast<void *>(2);
  const unsigned long started = clockMs;
  connectAt = clockMs + 33000;
  assert(HDS_OTA_PENDING_WIFI_TIMEOUT_MS == HDS_OTA_WIFI_TIMEOUT_MS);
  assert(pullOtaEnsureWifi(HDS_OTA_PENDING_WIFI_TIMEOUT_MS));
  assert(clockMs - started == 33000 && reconnects >= 2 && setupCalls == 0);
  resetConnection();
  b_ota = true;
  currentTask = reinterpret_cast<void *>(2);
  const unsigned long unavailableStarted = clockMs;
  assert(!pullOtaEnsureWifi(HDS_OTA_PENDING_WIFI_TIMEOUT_MS));
  assert(clockMs - unavailableStarted == HDS_OTA_WIFI_TIMEOUT_MS);
  assert(message == "WiFi failed");
}
void resetTask() {
  resetConnection();
  WiFi.stationStatus = WL_CONNECTED;
  startedTasks = deletedTasks = customRuns = pairings = resumes = 0;
  taskCreateOk = pauseReady = relinkConfirmed = true;
  filesystemRecoveryActive.store(false);
  wsPendingMask = 0;
  message.clear();
}
void testCustomAdmission() {
  for (const bool relink : {false, true}) {
    for (const bool interactive : {false, true}) {
      resetTask();
      wifiOperationReserved = true;
      customBuildStart(relink, interactive);
      assert(startedTasks == 0 && !b_ota && !b_pullOtaRunning);
      assert(wifiOperationReserved && message == "WiFi busy");
      resetTask();
      customBuildStart(relink, interactive);
      assert(startedTasks == 1 && wifiOperationReserved && b_ota);
      assert(!wifiReserveExternalOperation());
    }
  }
  resetTask();
  taskCreateOk = false;
  customBuildStart(false);
  assert(!wifiOperationReserved && !b_ota && !b_pullOtaRunning);
  assert(message == "OTA task failed");
}
void testCustomCleanup() {
  for (const uintptr_t mode : {uintptr_t(0), uintptr_t(1), uintptr_t(2)}) {
    resetTask();
    customBuildStart(mode == 1, mode == 0);
    customBuildTask(reinterpret_cast<void *>(mode));
    assert(!wifiOperationReserved && !b_ota && !b_pullOtaRunning && deletedTasks == 1);
    assert(mode == 1 ? pairings == 1 && customRuns == 0
                     : customRuns == 1 && lastInteractive == (mode == 0));
  }
  resetTask();
  customBuildStart(false);
  pauseReady = false;
  customBuildTask(nullptr);
  assert(!wifiOperationReserved && !b_ota && message == "OTA runtime pause failed");
  resetTask();
  customBuildStart(true);
  relinkConfirmed = false;
  filesystemRecoveryActive.store(true);
  customBuildTask(reinterpret_cast<void *>(1));
  assert(!wifiOperationReserved && pairings == 0 && resumes == 1);
  resetTask();
  customBuildStart(false);
  wsPendingMask = WSP_OTA_RESET;
  customBuildTask(nullptr);
  assert(wifiOperationReserved && b_ota && b_pullOtaRunning);
}
int main() {
  testOtaReconnect();
  testReconnectExclusions();
  testPendingWifiStartup();
  testCustomAdmission();
  testCustomCleanup();
}
'''
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp, binary = root / "ota-wifi.cpp", root / "ota-wifi.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("OTA WiFi reconnect, bounded startup, exclusion, and cleanup runtime tests passed")


if __name__ == "__main__":
    main()
