from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_custom_build_ota_contract import function_body
import configure_custom_build as custom_build
import generate_custom_ota_manifest as custom_manifest
import generate_release_manifest as release_manifest


ROOT = Path(__file__).resolve().parents[1]


def main():
    ota = (ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
    recovery = (ROOT / "include/filesystem_recovery.h").read_text(encoding="utf-8")
    rollback = (ROOT / "include/ota_rollback.h").read_text(encoding="utf-8")
    web = (ROOT / "include/webserver.h").read_text(encoding="utf-8")
    custom = (ROOT / "include/custom_build_ota.h").read_text(encoding="utf-8")
    setup = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    loader = function_body(ota, "bool pullOtaLoadPendingLittleFs(")
    assert 'preferences.getBool("paused", false) && filesystemRecoveryActive.load()' in loader
    assert "filesystemRecoveryLoad();" in function_body(rollback, "void hdsOtaRollbackBegin(")
    assert "filesystemRecoveryActive.load()" in function_body(custom, "bool customBuildCheckIn(")
    assert "assignment.combinationHash == installedCombination && !filesystemRecoveryActive.load()" in custom
    assert "currentCompare != 0 || currentIsPrerelease || filesystemRecoveryActive.load()" in ota
    assert "if (filesystemRecoveryActive.load())" in setup
    assert web.index('server.serveStatic("/", LittleFS, "/")') < web.index('server.on("/", HTTP_GET')
    assert "return webFilesystemReady.load() && !filesystemRecoveryActive.load();" in web
    assert "#if !HDS_FEATURE_LITTLEFS" not in web
    assert 'response->addHeader("Cache-Control", "no-store")' in web
    assert "while (true)" not in function_body(ota, "bool pullOtaRecoveryError() {")
    assert "pullOtaPartitionShaMatches(recoveryFirmware, esp_ota_get_running_partition())" in loader
    assert "!loaded.restore && !loaded.forwardRecovery" in loader
    assert 'preferences.putString("fw_sha", manifest.firmware.sha256)' in ota
    assert 'preferences.putUInt("fw_size", (uint32_t)manifest.firmware.size)' in ota

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "include").mkdir()
        header = root / "include/pull_ota_version.h"
        config = root / "include/config.h"
        features = {"features": ["pull-ota"]}
        assert release_manifest.forward_recovery_version(config) == 0
        assert custom_build.forwardRecoveryVersion(features, root) == 0
        for version in (0, 1, 2):
            header.write_text(f"#define HDS_OTA_FORWARD_RECOVERY_VERSION {version}\n", encoding="utf-8")
            assert release_manifest.forward_recovery_version(config) == int(version == 1)
            assert custom_build.forwardRecoveryVersion(features, root) == int(version == 1)
            assert custom_build.forwardRecoveryVersion({"features": ["wifi"]}, root) == 0
        for name in ("firmware.bin", "littlefs.bin"):
            (root / name).write_bytes(b"test")
        for capability in (0, 1):
            manifest = release_manifest.build_manifest(root, "v3.1.14", "decentespresso/openscale", "hds", "3.1.13", forward_recovery=capability)
            assert manifest.get("forward_recovery", 0) == capability
            build = {"combination_hash": "a" * 64, "firmware_version": "3.1.14-custom", "forward_recovery": capability}
            manifest = custom_manifest.customManifest(root, build, "https://example.com")
            assert manifest["forward_recovery"] == capability

    source = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#define CONFIG_APP_ROLLBACK_ENABLE 1
using String = std::string;
volatile std::atomic<bool> filesystemRecoveryActive{false}, webFilesystemReady{true};
std::map<String, bool> nvs;
std::vector<String> events;
bool storageOk = true, acceptOk = true, clearOk = true;
bool hdsOtaPendingVerify = true;
bool b_ota = true;
const int ESP_OK = 0;
struct Preferences {
  String name;
  bool begin(const char *value, bool) { name = value; return storageOk; }
  bool getBool(const char *key, bool fallback) {
    const auto found = nvs.find(name + key);
    return found == nvs.end() ? fallback : found->second;
  }
  int putBool(const char *key, bool value) { nvs[name + key] = value; return 1; }
  void end() {}
};
struct Logger { void println(const char *) {};} Serial;
int esp_ota_mark_app_valid_cancel_rollback() {
  assert(nvs["ota_recoveryactive"]);
  events.push_back("accept-recovery");
  return acceptOk ? ESP_OK : -1;
}
void hdsOtaClearAttempts() {}
void pullOtaPauseFilesystemServices() { webFilesystemReady.store(false); events.push_back("stop-fs"); }
String pullOtaCurrentCombinationHash() { return "build"; }
void customBuildReportInstallState(const String &, const char *state) { events.push_back(state); }
bool pullOtaFail(const char *, const char * = "") { events.push_back("error"); return false; }
struct PullOtaPendingLittleFs {
  bool restore = false, restoreAttempted = false, filesystemDirty = false;
  bool forwardRecovery = false;
  uint8_t targetAttempts = 0;
};
PullOtaPendingLittleFs saved;
bool verified = false, succeeds = false, activateOk = true, pending = true;
int attempts = 0;
bool pullOtaLoadPendingLittleFs(PullOtaPendingLittleFs &out) { out = saved; return pending; }
bool pullOtaVerifyPendingLittleFs(const PullOtaPendingLittleFs &) { return verified; }
bool pullOtaBeginRollbackLittleFsAttempt() { return true; }
bool pullOtaBeginTargetLittleFsAttempt(uint8_t) { return true; }
bool pullOtaAttemptPendingLittleFs(const PullOtaPendingLittleFs &, bool &dirty) {
  if (saved.forwardRecovery) assert(!hdsOtaPendingVerify && nvs["ota_recoveryactive"]);
  attempts++; dirty = saved.filesystemDirty; return succeeds;
}
bool pullOtaActivateRollbackLittleFs(const PullOtaPendingLittleFs &) { events.push_back("rollback"); return activateOk; }
void hdsOtaRollbackMarkValid() { events.push_back("valid"); }
void customBuildReportInstalled() { assert(!filesystemRecoveryActive.load()); events.push_back("installed"); }
bool pullOtaClearPendingLittleFs() { events.push_back("clear"); return clearOk; }
void pullOtaDraw(const char *, const char *) {}
void delay(int) {}
unsigned long millis() { return 0; }
void remoteQueueOtaResetAt(unsigned long) { events.push_back("restart"); }
void reset() {
  filesystemRecoveryActive.store(false); webFilesystemReady.store(true);
  nvs.clear(); events.clear(); saved = {};
  storageOk = acceptOk = clearOk = hdsOtaPendingVerify = b_ota = activateOk = pending = true;
  verified = succeeds = false; attempts = 0;
}
'''
    source += re.sub(r"^#include[^\n]*", "", recovery, flags=re.MULTILINE)
    for text, declaration in (
        (rollback, "bool hdsOtaAcceptFilesystemRecovery()"),
        (ota, "bool pullOtaRecoveryError()"),
        (ota, "bool pullOtaResumePendingLittleFs()"),
    ):
        source += declaration + " {" + function_body(text, declaration + " {") + "}\n"
    source += r'''
int main() {
  reset(); saved.restore = true;
  assert(pullOtaResumePendingLittleFs());
  assert(attempts == 1 && filesystemRecoveryActive.load() && !webFilesystemReady.load());
  assert((events == std::vector<String>{"stop-fs", "accept-recovery", "failed", "error"}));
  assert(!hdsOtaPendingVerify && nvs["ota_fspaused"]);
  filesystemRecoveryActive.store(false); filesystemRecoveryLoad();
  assert(filesystemRecoveryActive.load());
  reset(); saved.restore = saved.restoreAttempted = true;
  assert(pullOtaResumePendingLittleFs() && attempts == 0);
  reset(); saved.filesystemDirty = true;
  assert(!pullOtaResumePendingLittleFs() && attempts == 2);
  assert((events == std::vector<String>{"rollback"}));
  reset();
  assert(!pullOtaResumePendingLittleFs() && attempts == 2 && events.empty());
  reset(); saved.filesystemDirty = true; activateOk = false;
  assert(pullOtaResumePendingLittleFs() && filesystemRecoveryActive.load());
  reset(); saved.restore = true; storageOk = false;
  assert(!pullOtaResumePendingLittleFs() && hdsOtaPendingVerify);
  reset(); saved.restore = true; acceptOk = false;
  assert(!pullOtaResumePendingLittleFs() && hdsOtaPendingVerify);
  reset(); filesystemRecoveryStore(true); verified = true;
  assert(pullOtaResumePendingLittleFs() && attempts == 0);
  assert(!filesystemRecoveryActive.load() && !nvs["ota_recoveryactive"]);
  assert((events == std::vector<String>{"valid", "clear", "installed", "restart"}));
  reset(); verified = true; clearOk = false;
  assert(pullOtaResumePendingLittleFs() && filesystemRecoveryActive.load());
  for (const auto &event : events) assert(event != "installed" && event != "restart");
  reset(); saved.forwardRecovery = true;
  assert(pullOtaResumePendingLittleFs() && attempts == 2);
  assert(filesystemRecoveryActive.load() && nvs["ota_fspaused"]);
  for (const auto &event : events) assert(event != "rollback" && event != "installed");
  reset(); saved.forwardRecovery = true; succeeds = true;
  assert(pullOtaResumePendingLittleFs() && attempts == 1);
  assert(!filesystemRecoveryActive.load() && events.back() == "restart");
  reset(); saved.forwardRecovery = true; acceptOk = false;
  assert(!pullOtaResumePendingLittleFs() && attempts == 0);
  reset(); saved.forwardRecovery = true; saved.targetAttempts = 2;
  assert(pullOtaResumePendingLittleFs() && attempts == 0 && nvs["ota_fspaused"]);
}
'''
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        cpp, binary = path / "recovery.cpp", path / "recovery.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("filesystem recovery runtime tests passed")
    script = web.split("<script>", 1)[1].split("</script>", 1)[0]
    subprocess.run(["node", "-e", r'''
const assert = require('node:assert/strict');
const vm = require('node:vm');
const forms = {wifi: {data: {ssid: 'network', pass: 'secret'}}, name: {data: {name: 'scale'}}, status: {}};
const calls = [];
let ok = true;
vm.runInNewContext(process.argv[1], {
  document: {getElementById: id => forms[id]},
  name: '', status: '',
  FormData: class { constructor(form) { return Object.entries(form.data); } },
  fetch: async (path, options) => { calls.push([path, JSON.parse(options.body)]); return {ok, status: 500}; }
});
(async () => {
  for (const id of ['wifi', 'name']) {
    forms[id].onsubmit({preventDefault() {}, currentTarget: forms[id]});
    await new Promise(resolve => setImmediate(resolve));
    assert.deepEqual(calls.at(-1), ['/setup/' + id, forms[id].data]);
    assert.equal(forms.status.textContent, 'Saved. Restarting.');
  }
  ok = false;
  forms.wifi.onsubmit({preventDefault() {}, currentTarget: forms.wifi});
  await new Promise(resolve => setImmediate(resolve));
  assert.match(String(forms.status.textContent), /500/);
  console.log('fallback HTTP page form tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
''', script], check=True)


if __name__ == "__main__":
    main()
