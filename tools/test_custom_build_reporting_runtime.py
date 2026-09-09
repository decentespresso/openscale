from pathlib import Path
import shutil
import subprocess
import tempfile

from test_custom_build_ota_contract import function_body


ROOT = Path(__file__).resolve().parents[1]


def main():
    custom = (ROOT / "include/custom_build_ota.h").read_text(encoding="utf-8")
    ota = (ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
    report = function_body(custom, "void customBuildReportInstalled()")
    reportState = function_body(custom, "void customBuildReportInstallState(")
    install = function_body(ota, "bool pullOtaInstall(")
    resume = function_body(ota, "bool pullOtaResumePendingLittleFs()")
    source = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
using String = std::string;
struct CustomBuildAssignment {};
struct PullOtaManifest { int firmware = 0; };
bool b_ota, storeOk, streamOk;
const int U_FLASH = 0;
struct PullOtaPendingLittleFs {
  bool restore = false;
  bool restoreAttempted = false;
  uint8_t targetAttempts = 0;
  bool filesystemDirty = false;
};
bool paired, network, clockReady, pending, verified, succeeds;
int reports, failedReports, retries;
String identity, reported;
std::vector<String> events;
struct Logger { void println(const char *) {};} Serial;
bool customBuildRelinkAvailable() { return paired; }
void pullOtaDraw(const char *, const char *, const char * = "") {}
bool pullOtaEnsureWifi() { return network; }
bool pullOtaClockReady() { return clockReady; }
String pullOtaCurrentCombinationHash() { return identity; }
bool customBuildCheckIn(const String &value, CustomBuildAssignment &,
    const String &combination = "", const char *state = "installing") {
  reports++;
  reported = value;
  events.push_back(combination.empty() ? "report" : state);
  return reports > failedReports;
}
void delay(int) { retries++; }
bool pullOtaLoadPendingLittleFs(PullOtaPendingLittleFs &) { return pending; }
bool pullOtaVerifyPendingLittleFs(const PullOtaPendingLittleFs &) { return verified; }
bool pullOtaBeginRollbackLittleFsAttempt() { return true; }
bool pullOtaBeginTargetLittleFsAttempt(uint8_t) { return true; }
void pullOtaRecoveryError() { assert(false); }
bool pullOtaAttemptPendingLittleFs(const PullOtaPendingLittleFs &, bool &) { return succeeds; }
bool pullOtaActivateRollbackLittleFs(const PullOtaPendingLittleFs &) { return true; }
void hdsOtaRollbackMarkValid() { events.push_back("valid"); }
bool pullOtaClearPendingLittleFs() { events.push_back("clear"); return true; }
bool pullOtaFail(const char *) { return false; }
bool pullOtaStorePendingLittleFs(const PullOtaManifest &, const PullOtaManifest &,
    const String &, const String &) { events.push_back("store"); return storeOk; }
bool pullOtaStreamAsset(int, int, const char *) { events.push_back("stream"); return streamOk; }
unsigned long millis() { return 0; }
void remoteQueueOtaResetAt(unsigned long) { events.push_back("restart"); }
void reset() {
  paired = network = clockReady = pending = verified = true;
  succeeds = false;
  storeOk = streamOk = true;
  reports = failedReports = retries = 0;
  identity = String(64, 'a');
  reported.clear();
  events.clear();
}
'''
    source += "void customBuildReportInstalled() {" + report + "}\n"
    source += "void customBuildReportInstallState(const String &combinationHash, const char *state) {" + reportState + "}\n"
    source += "bool pullOtaInstall(const PullOtaManifest &manifest, const PullOtaManifest &rollbackManifest, const String &combinationHash, const String &rollbackCombinationHash) {" + install + "}\n"
    source += "bool pullOtaResumePendingLittleFs() {" + resume + "}\n"
    source += r'''
int main() {
  reset();
  assert(pullOtaInstall({}, {}, String(64, 'b'), identity));
  assert(reported == identity);
  assert((events == std::vector<String>{"store", "installing", "stream", "restart"}));
  reset();
  storeOk = false;
  assert(!pullOtaInstall({}, {}, String(64, 'b'), identity));
  assert(reports == 0);
  reset();
  streamOk = false;
  assert(!pullOtaInstall({}, {}, String(64, 'b'), identity));
  assert((events == std::vector<String>{"store", "installing", "stream", "failed", "clear"}));
  reset();
  failedReports = 100;
  assert(pullOtaInstall({}, {}, String(64, 'b'), identity));
  reset();
  assert(pullOtaInstall({}, {}, "", identity));
  assert(reports == 0);
  reset();
  assert(pullOtaResumePendingLittleFs());
  assert(reported == identity);
  assert((events == std::vector<String>{"valid", "report", "clear", "restart"}));
  reset();
  verified = false;
  assert(!pullOtaResumePendingLittleFs());
  assert(reports == 0);
  reset();
  verified = false;
  succeeds = true;
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 1);
  reset();
  pending = false;
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 0);
  reset();
  paired = false;
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 0);
  reset();
  identity.clear();
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 1 && reported.empty());
  reset();
  failedReports = 2;
  customBuildReportInstalled();
  assert(reports == 3 && retries == 2);
  reset();
  failedReports = 100;
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 3 && events.back() == "restart");
  reset();
  network = false;
  assert(pullOtaResumePendingLittleFs());
  assert(reports == 0 && events.back() == "restart");
  reset();
  clockReady = false;
  customBuildReportInstalled();
  assert(reports == 0);
}
'''
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        cpp = path / "report.cpp"
        binary = path / "report.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("custom build reporting runtime tests passed")


if __name__ == "__main__":
    main()
