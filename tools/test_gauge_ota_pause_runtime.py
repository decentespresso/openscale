from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_ota_runtime_isolation_contract import body_at, function_body


ROOT = Path(__file__).resolve().parents[1]


def check_menu_escape():
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    battery = (ROOT / "include/fuel_gauge_menu.h").read_text(encoding="utf-8")
    for text, declaration, marker, exit_statement in (
        (menu, "void waitForMenuButtonRelease() {", "while (digitalRead", "return;"),
        (menu, "void showWifiStatus() {", "while (b_showWifiData)", "return;"),
        (menu, "void showStatus() {", "while (b_showStatusData)", "return;"),
        (menu, "void showAbout() {", "while (b_showAbout)", "return;"),
        (menu, "void showLogo() {", "while (b_showLogo)", "return;"),
        (battery, "void showBatInfo() {", "while (showing)", "return;"),
        (menu, "bool grinderEnsureWifiReadyForDiscovery() {", "while (WiFi.status()", "return false;"),
        (menu, "void grinderSelectPlugMenu() {", "while (selecting)", "return;"),
        (menu, "static inline float grinderHandleDraftAdjust(", "while (digitalRead", "return draft;"),
        (menu, "static inline bool grinderEditNumber(", "while (true)", "return false;"),
        (menu, "bool calibrationCaptureRaw(", "while (millis()", "return false;"),
    ):
        body = body_at(text, text.index(declaration))
        blocking = body_at(body, body.index(marker)).strip()
        assert blocking.startswith(f"if (b_ota) {exit_statement}"), declaration
    calibration = body_at(menu, menu.index("void calibration(int input) {"))
    captures = list(re.finditer(r"if \(!calibrationCaptureRaw\(", calibration))
    assert len(captures) == 2
    for capture in captures:
        failure = body_at(calibration, capture.start())
        interrupted = body_at(failure, failure.index("if (b_ota)"))
        assert re.sub(r"\s+", " ", interrupted).strip() == "calibrationFinish(false); return;"
        assert failure.index("if (b_ota)") < failure.index("calibrationFail(")


def main():
    check_menu_escape()
    wifi = (ROOT / "include/wifi_ota.h").read_text(encoding="utf-8")
    pull = (ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
    parameter = (ROOT / "include/parameter.h").read_text(encoding="utf-8")
    loop = function_body((ROOT / "src/hds.ino").read_text(encoding="utf-8"), "loop")
    main_ota = body_at(loop, loop.index("if (b_ota)"))
    assert main_ota.index('wakeScaleFromSoftSleep("OTA wake");') < main_ota.index("fuelGaugeRequestQuiesce();")
    rollback = (ROOT / "include/ota_rollback.h").read_text(encoding="utf-8")
    rollback_begin = function_body(rollback, "hdsOtaRollbackBegin")
    assert "hdsOtaRollback(" not in rollback_begin
    setup = function_body((ROOT / "src/hds.ino").read_text(encoding="utf-8"), "setup")
    rollback_at = setup.index("if (bootRollbackReason) hdsOtaRollback(bootRollbackReason);")
    assert setup.index("bootRollbackReason = hdsOtaRollbackBegin(r);") < setup.index("storageInit()")
    assert setup.index("esp32_sleep();") < setup.index("fuelGaugeStartDetection(") < rollback_at
    assert setup.index("fuelGaugeBegin(") < rollback_at < setup.index("ADS_init();")
    timeout = re.search(r"OTA_RUNTIME_PAUSE_TIMEOUT_MS = (\d+)", parameter)
    assert timeout and int(timeout.group(1)) == 10000
    for name, first_effect in (
        ("pullOtaStreamAsset", "HTTPClient http;"),
        ("pullOtaInstall", "params.prepareLegacyDowngrade()"),
        ("pullOtaResumePendingLittleFs", "pullOtaLoadPendingLittleFs"),
    ):
        body = function_body(pull, name)
        assert body.index("if (!otaRuntimeIsPaused())") < body.index(first_effect)
        assert "fuelGauge" not in body
    source = r'''
#include <cassert>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#define HDS_ENABLE_ENERGY_MENU 0
#define HDS_FEATURE_WEBSOCKET 0
#define HDS_FEATURE_ELEGANT_OTA 0
using String = std::string;
constexpr unsigned long OTA_RUNTIME_PAUSE_TIMEOUT_MS = @TIMEOUT@;
constexpr int UPDATE_SIZE_UNKNOWN = 0, U_FLASH = 0;
std::mutex otaDispatchMutex;
bool b_ota = false, b_pullOtaRunning = false, elegantOtaUploadClaimed = false;
bool cleanupSucceeds = true, paused = false, losePause = false;
bool b_softSleep = false, otaBleClientDisconnected = false, otaTransportsStopped = false;
bool ownerRunning = false;
bool cleanupRequested = false;
enum class FuelGaugeQuiesceStatus { Running, Pending, Complete, Failed };
FuelGaugeQuiesceStatus cleanupStatus = FuelGaugeQuiesceStatus::Running;
unsigned long clockMs = 0, pauseStartedAt = 0, cleanupDuration = 3500;
unsigned long flashStartedAt = 0;
int begins = 0, failures = 0, pauseReads = 0;
unsigned long millis() { return clockMs; }
void serviceMainOta();
void delay(unsigned long elapsed) {
  clockMs += elapsed;
  ownerRunning = true;
  serviceMainOta();
  ownerRunning = false;
}
bool otaRuntimeIsPaused() {
  if (losePause) return ++pauseReads == 1;
  return paused;
}
void fuelGaugeRequestQuiesce() { assert(ownerRunning && !b_softSleep); cleanupRequested = true; }
void serviceFuelGauge() {
  assert(ownerRunning && cleanupRequested);
  cleanupStatus = cleanupSucceeds && clockMs - pauseStartedAt >= cleanupDuration
      ? FuelGaugeQuiesceStatus::Complete
      : !cleanupSucceeds && clockMs - pauseStartedAt >= 8000
          ? FuelGaugeQuiesceStatus::Failed : FuelGaugeQuiesceStatus::Pending;
}
FuelGaugeQuiesceStatus fuelGaugeQuiesceStatus() { assert(ownerRunning); return cleanupStatus; }
bool blePauseForOta() { return false; }
bool bleHasLiveClient() { return false; }
void wakeScaleFromSoftSleep(const char *) {
  assert(ownerRunning && !cleanupRequested);
  cleanupStatus = FuelGaugeQuiesceStatus::Running;
  b_softSleep = false;
}
void setOtaRuntimePaused(bool value) {
  assert(!value || cleanupStatus == FuelGaugeQuiesceStatus::Complete);
  paused = value;
}
void serviceMainOta() {
@MAIN_OTA@
}
using esp_reset_reason_t = int;
constexpr uint32_t HDS_OTA_MAX_VERIFY_ATTEMPTS = 1;
bool hdsOtaPendingVerify = false, bootPendingVerify = false;
uint32_t bootAttempts = 0;
int bootLoads = 0, bootRecords = 0, bootClears = 0;
void filesystemRecoveryLoad() { ++bootLoads; }
bool hdsOtaIsPendingVerify() { return bootPendingVerify; }
void hdsOtaClearAttempts() { ++bootClears; }
uint32_t hdsOtaRecordAttempt() { ++bootRecords; return ++bootAttempts; }
bool hdsOtaResetLooksCrashy(int reason) { return reason == 1; }
struct {
  void print(const char *) {}
  void println(uint32_t) {}
} Serial;
const char *hdsOtaRollbackBegin(esp_reset_reason_t resetReason) {
@ROLLBACK_BEGIN@
}
void onOTAStart() { b_ota = true; paused = false; pauseStartedAt = clockMs; }
void onOTAEnd(bool success) { assert(!success); ++failures; b_ota = false; }
struct Parameter {
  String text;
  String value() const { return text; }
};
struct AsyncWebServerRequest {
  std::map<String, Parameter> params;
  int status = 0;
  String message;
  bool hasParam(const char *name) const { return params.count(name) != 0; }
  const Parameter *getParam(const char *name) const { return &params.at(name); }
  void send(int code, const char *, const char *body) { status = code; message = body; }
};
struct Updater {
  bool beginOk = true;
  bool begin(int, int) {
    assert(paused || losePause);
    ++begins;
    flashStartedAt = clockMs;
    return beginOk;
  }
  bool setMD5(const char *) { return true; }
  void abort() {}
  const char *errorString() const { return "update failed"; }
} Update;
void handleElegantOtaStart(AsyncWebServerRequest *request) {
@START@
}
void reset(unsigned long start = 0) {
  clockMs = start;
  b_ota = b_pullOtaRunning = elegantOtaUploadClaimed = paused = losePause = false;
  cleanupSucceeds = true;
  cleanupStatus = FuelGaugeQuiesceStatus::Running;
  otaBleClientDisconnected = otaTransportsStopped = ownerRunning = false;
  b_softSleep = cleanupRequested = false;
  cleanupDuration = 3500;
  begins = failures = pauseReads = 0;
  Update.beginOk = true;
}
int main() {
  assert(hdsOtaRollbackBegin(0) == nullptr && bootLoads == 1);
  assert(bootClears == 1 && bootRecords == 0);
  bootPendingVerify = true;
  assert(hdsOtaRollbackBegin(0) == nullptr && bootAttempts == 1 && bootRecords == 1);
  assert(String(hdsOtaRollbackBegin(0)) == "boot attempts" && bootAttempts == 2);
  bootAttempts = 0;
  assert(String(hdsOtaRollbackBegin(1)) == "reset reason" && bootAttempts == 1);
  assert(String(hdsOtaRollbackBegin(1)) == "boot attempts" && bootAttempts == 2);
  for (const unsigned long start : {0UL, std::numeric_limits<unsigned long>::max() - 1000}) {
    for (const bool sleeping : {false, true}) {
      reset(start);
      b_softSleep = sleeping;
      AsyncWebServerRequest request;
      handleElegantOtaStart(&request);
      assert(request.status == 200 && begins == 1 && failures == 0);
      assert(flashStartedAt - start == 3500 && !b_softSleep);
    }
  }
  for (const bool finishesTooLate : {false, true}) {
    reset();
    cleanupSucceeds = finishesTooLate;
    cleanupDuration = 10001;
    AsyncWebServerRequest request;
    handleElegantOtaStart(&request);
    assert(request.status == 503 && begins == 0 && failures == 1);
    assert(clockMs == 10000);
  }
  reset();
  losePause = true;
  AsyncWebServerRequest lost;
  handleElegantOtaStart(&lost);
  assert(lost.status == 503 && begins == 0 && failures == 1);
  reset();
  b_pullOtaRunning = true;
  AsyncWebServerRequest busy;
  handleElegantOtaStart(&busy);
  assert(busy.status == 409 && begins == 0);
  reset();
  AsyncWebServerRequest filesystem;
  filesystem.params["mode"] = Parameter{"fs"};
  handleElegantOtaStart(&filesystem);
  assert(filesystem.status == 400 && begins == 0);
}
'''.replace("@TIMEOUT@", timeout.group(1)).replace(
        "@START@", function_body(wifi, "handleElegantOtaStart")).replace(
        "@MAIN_OTA@", main_ota).replace("@ROLLBACK_BEGIN@", rollback_begin)
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp, binary = root / "gauge-ota.cpp", root / "gauge-ota.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("gauge OTA cleanup admission, rollover, and no-flash timeout tests passed")


if __name__ == "__main__":
    main()
