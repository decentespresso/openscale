from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    ota = (ROOT / "include/pull_ota.h").read_text(encoding="utf-8")
    selection = block_after(ota, "void pullOtaBuildSelectableReleases(")
    custom = block_after((ROOT / "include/custom_build_ota.h").read_text(encoding="utf-8"),
                         "void customBuildRun(bool interactive = true) {")
    source = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include "pull_ota_target.h"
#include "pull_ota_version.h"

using String = std::string;
constexpr uint8_t HDS_OTA_MAX_RELEASE_CHOICES = 10;
struct PullOtaManifest {
  String version;
  struct { bool present = false; } littlefs;
  int forwardRecoveryVersion = 0, firmware = 0;
};
struct PullOtaReleaseList {
  PullOtaManifest releases[HDS_OTA_MAX_RELEASE_CHOICES];
  uint8_t count = 0;
};
struct PullOtaReleaseSelection {
  uint8_t indices[HDS_OTA_MAX_RELEASE_CHOICES] = {};
  uint8_t count = 0;
};
struct CustomBuildAssignment {
  bool linked = true;
  bool identityRejected = false;
  String state = "ready";
  String combinationHash;
};
struct Probe {
  int installs = 0, customFetches = 0, officialFetches = 0, pairingCalls = 0;
  int catalogFetches = 0, streams = 0, reboots = 0, clears = 0, resumes = 0;
  int pickerCalls = 0, confirms = 0, legacyPreparations = 0;
  String targetVersion, rollbackVersion, targetHash, rollbackHash, message;
};
const String hashA(64, 'a'), hashB(64, 'b');
String currentVersion, currentHash;
std::atomic<bool> filesystemRecoveryActive{false};
CustomBuildAssignment assignment;
Probe probe;
bool paired = true, signatureValid = true, confirmInstall = true;
bool rollbackAvailable = true, signedCatalogValid = true, pending = false;
bool storageReady = true, streamReady = true, credentialsReady = true, b_ota = false;
PullOtaReleaseList offeredCatalog;
struct {
  bool prepareLegacyDowngrade() { ++probe.legacyPreparations; return credentialsReady; }
} params;
constexpr int U_FLASH = 0;

String pullOtaCurrentVersion() { return currentVersion; }
String pullOtaCurrentCombinationHash() { return currentHash; }
int pullOtaCompareVersions(const String &left, const String &right) {
  return pullOtaCompareVersionPrefixes(left.c_str(), right.c_str());
}
void pullOtaBuildSelectableReleases(const PullOtaReleaseList &catalog,
                                  PullOtaReleaseSelection &selection) { @SELECTION@ }

bool customBuildLoadCredentials(String &, String &, bool *initialized) {
  *initialized = paired;
  return true;
}
bool customBuildPairScale(bool) { ++probe.pairingCalls; return true; }
void customBuildShowStatus(const char *message, const char *) { probe.message = message; }
void pullOtaDraw(const char *message, const char * = "", const char * = "") {
  if (String(message) == "No newer stable") probe.message = message;
}
bool pullOtaEnsureWifi() { return true; }
bool pullOtaClockReady() { return true; }
bool pullOtaFail(const char *message, const char * = "") { probe.message = message; return false; }
bool customBuildCheckIn(const String &installed, CustomBuildAssignment &output) {
  assert(installed == currentHash);
  output = assignment;
  return true;
}
void customBuildHashPrefix(const String &hash, char output[9]) {
  assert(hash.length() == 64);
  memcpy(output, hash.c_str(), 8);
  output[8] = '\0';
}
bool customBuildFetchManifest(const String &hash, PullOtaManifest &manifest) {
  ++probe.customFetches;
  assert(hash == assignment.combinationHash || hash == currentHash);
  manifest.version = "3.1.14-custom";
  manifest.littlefs.present = true;
  return signatureValid && (hash != currentHash || rollbackAvailable);
}
bool customBuildConfirmInstall(const PullOtaManifest &, const String &) { return confirmInstall; }
bool pullOtaFetchCurrentReleaseManifest(PullOtaManifest &manifest) {
  ++probe.officialFetches;
  manifest.version = currentVersion;
  manifest.littlefs.present = true;
  return true;
}
bool pullOtaStorePendingLittleFs(const PullOtaManifest &target, const PullOtaManifest &rollback,
                               const String &targetHash, const String &rollbackHash) {
  ++probe.installs;
  probe.targetVersion = target.version;
  probe.rollbackVersion = rollback.version;
  probe.targetHash = targetHash;
  probe.rollbackHash = rollbackHash;
  return storageReady;
}
bool pullOtaStreamAsset(int, int command, const char *) {
  assert(command == U_FLASH);
  ++probe.streams;
  return streamReady;
}
void customBuildReportInstallState(const String &, const char *) {}
bool pullOtaClearPendingLittleFs() { ++probe.clears; return true; }
void delay(int) {}
unsigned long millis() { return 0; }
void remoteQueueOtaResetAt(unsigned long) { ++probe.reboots; }
bool pullOtaInstall(const PullOtaManifest &manifest, const PullOtaManifest &rollbackManifest,
                    const String &combinationHash = "", const String &rollbackCombinationHash = "") { @INSTALL@ }
void customBuildRun(bool interactive = true) { @CUSTOM@ }
bool pullOtaHasPendingLittleFs() { return pending; }
bool pullOtaResumePendingLittleFs() { ++probe.resumes; return true; }
bool pullOtaFetchSignedManifest(String &body) {
  ++probe.catalogFetches;
  body = "verified-catalog-fixture";
  return signedCatalogValid;
}
bool pullOtaParseManifest(const String &, PullOtaReleaseList &catalog) {
  catalog = offeredCatalog;
  return true;
}
bool pullOtaFindCurrentRelease(const PullOtaReleaseList &, PullOtaManifest &) { return false; }
bool pullOtaHasNewerRelease(const PullOtaReleaseList &catalog,
                           const PullOtaReleaseSelection &selection) { @NEWER@ }
bool pullOtaFindTargetRelease(const PullOtaReleaseList &catalog,
                            const PullOtaReleaseSelection &selection,
                            const PullOtaTargetVersion &target,
                            uint8_t *selectedCatalogIndex) { @TARGET@ }
bool pullOtaPickRelease(const PullOtaReleaseList &, const PullOtaReleaseSelection &selection,
                       uint8_t *selectedCatalogIndex) {
  ++probe.pickerCalls;
  *selectedCatalogIndex = selection.indices[0];
  return true;
}
bool pullOtaConfirmInstall(const PullOtaManifest &) { ++probe.confirms; return confirmInstall; }
void pullOtaRunUpdate(const PullOtaTargetVersion &target) { @RUN@ }

void resetProbe(const String &version, const String &hash = "") {
  currentVersion = version;
  currentHash = hash;
  filesystemRecoveryActive.store(false);
  assignment = CustomBuildAssignment{};
  assignment.combinationHash = hashB;
  probe = Probe{};
  paired = signatureValid = confirmInstall = true;
  rollbackAvailable = signedCatalogValid = storageReady = streamReady = credentialsReady = true;
  pending = b_ota = false;
  offeredCatalog = PullOtaReleaseList{};
  offeredCatalog.count = 1;
  offeredCatalog.releases[0].version = "3.1.14";
  offeredCatalog.releases[0].littlefs.present = true;
}

void testReleaseSelection() {
  PullOtaReleaseList catalog;
  catalog.count = 3;
  catalog.releases[0].version = "3.1.15";
  catalog.releases[1].version = "3.1.14";
  catalog.releases[2].version = "3.1.13";
  for (const String &version : {String("3.1.14"), String("3.1.14-custom")}) {
    resetProbe(version);
    PullOtaReleaseSelection selection;
    pullOtaBuildSelectableReleases(catalog, selection);
    assert(selection.count == 2 && selection.indices[0] == 0 && selection.indices[1] == 2);
    currentHash = hashA;
    selection = PullOtaReleaseSelection{};
    pullOtaBuildSelectableReleases(catalog, selection);
    assert(selection.count == 2 && selection.indices[0] == 0 && selection.indices[1] == 1);
  }
  for (const String &version : {String("3.1.14-preview.1"), String("3.1.14-rc.1"), String("3.1.14")}) {
    resetProbe(version);
    filesystemRecoveryActive.store(version == "3.1.14");
    PullOtaReleaseSelection selection;
    pullOtaBuildSelectableReleases(catalog, selection);
    assert(selection.count == (version == "3.1.14" ? 3 : 2) && selection.indices[1] == 1);
  }
}

void testLegacyDowngradeCompatibility() {
  for (const String &version : {String("3.1.14-custom"), String("3.1.14-preview.1"), String("3.1.14-rc.1")}) {
    for (const bool recovery : {false, true}) {
      for (const bool interactive : {false, true}) {
        resetProbe(version, version == "3.1.14-custom" ? hashA : "");
        filesystemRecoveryActive.store(recovery);
        offeredCatalog.releases[0].version = "3.1.13";
        pullOtaRunUpdate(interactive ? pullOtaNoTargetVersion() : pullOtaMakeTargetVersion(3, 1, 13));
        assert(probe.installs == 0 && probe.streams == 0 && probe.reboots == 0);
        assert(probe.pickerCalls == 0 && probe.confirms == 0 && probe.legacyPreparations == 0);
        if (!interactive) assert(probe.message == "Version not offered");
      }
    }
  }
  for (const bool interactive : {false, true}) {
    resetProbe("3.1.14");
    offeredCatalog.releases[0].version = "3.1.13";
    pullOtaRunUpdate(interactive ? pullOtaNoTargetVersion() : pullOtaMakeTargetVersion(3, 1, 13));
    assert(probe.installs == 1 && probe.streams == 1 && probe.reboots == 1);
    assert(probe.targetVersion == "3.1.13" && probe.rollbackVersion == "3.1.14");
    assert(probe.pickerCalls == int(interactive) && probe.confirms == int(interactive));
  }
}

void testOfficialToSameVersionCustom() {
  for (const bool interactive : {false, true}) {
    resetProbe("3.1.14");
    customBuildRun(interactive);
    assert(probe.installs == 1 && probe.customFetches == 1 && probe.officialFetches == 1);
    assert(probe.targetVersion == "3.1.14-custom" && probe.rollbackVersion == "3.1.14");
    assert(probe.targetHash == hashB && probe.rollbackHash.empty() && probe.pairingCalls == 0);
  }
}

void testCustomCombinationIdentity() {
  resetProbe("3.1.14-custom", hashA);
  customBuildRun(false);
  assert(probe.installs == 1 && probe.customFetches == 2 && probe.officialFetches == 0);
  assert(probe.targetVersion == probe.rollbackVersion);
  assert(probe.targetHash == hashB && probe.rollbackHash == hashA);
  resetProbe("3.1.14-custom", hashB);
  customBuildRun(false);
  assert(probe.installs == 0 && probe.customFetches == 0 && probe.officialFetches == 0);
  assert(probe.message == "Already installed");
  filesystemRecoveryActive.store(true);
  customBuildRun(false);
  assert(probe.installs == 1 && probe.targetHash == hashB && probe.rollbackHash == hashB);
}

void testAssignmentGuards() {
  resetProbe("3.1.14");
  signatureValid = false;
  customBuildRun(false);
  assert(probe.installs == 0 && probe.officialFetches == 0 && probe.message == "Signature failed");
  resetProbe("3.1.14");
  paired = false;
  customBuildRun(false);
  assert(probe.installs == 0 && probe.customFetches == 0 && probe.pairingCalls == 0);
  resetProbe("3.1.14");
  assignment.linked = false;
  customBuildRun(false);
  assert(probe.installs == 0 && probe.customFetches == 0);
  resetProbe("3.1.14");
  confirmInstall = false;
  customBuildRun(true);
  assert(probe.installs == 0 && probe.officialFetches == 0);
}

void testCustomToSameVersionOfficial() {
  for (const bool interactive : {false, true}) {
    resetProbe("3.1.14-custom", hashA);
    pullOtaRunUpdate(interactive ? pullOtaNoTargetVersion() : pullOtaMakeTargetVersion(3, 1, 14));
    assert(probe.catalogFetches == 1 && probe.customFetches == 1 && probe.officialFetches == 0);
    assert(probe.installs == 1 && probe.streams == 1 && probe.reboots == 1);
    assert(probe.targetVersion == "3.1.14" && probe.rollbackVersion == "3.1.14-custom");
    assert(probe.targetHash.empty() && probe.rollbackHash == hashA && probe.legacyPreparations == 1);
    assert(probe.pickerCalls == int(interactive) && probe.confirms == int(interactive));
    if (interactive) assert(probe.message == "No newer stable");
  }
  resetProbe("3.1.14-custom", hashA);
  rollbackAvailable = false;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.message == "Recovery unsupported" && probe.installs == 0 && probe.streams == 0);
  assert(probe.customFetches == 1 && probe.officialFetches == 0 && probe.legacyPreparations == 0);
  offeredCatalog.releases[0].forwardRecoveryVersion = HDS_OTA_FORWARD_RECOVERY_VERSION;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.installs == 1 && probe.streams == 1 && probe.reboots == 1);
  assert(probe.rollbackVersion.empty() && probe.rollbackHash == hashA);
}

void testOfficialRunGuards() {
  resetProbe("3.1.14-custom", hashA);
  signedCatalogValid = false;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.message == "Signature failed" && probe.customFetches == 0 && probe.installs == 0);
  resetProbe("3.1.14-custom", hashA);
  pending = true;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.resumes == 1 && probe.catalogFetches == 0 && probe.installs == 0);
  resetProbe("3.1.14-custom", hashA);
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 13));
  assert(probe.message == "Version not offered" && probe.installs == 0 && probe.pickerCalls == 0);
  resetProbe("3.1.14-custom", hashA);
  credentialsReady = false;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.message == "WiFi storage failed" && probe.installs == 0 && probe.streams == 0);
  resetProbe("3.1.14-custom", hashA);
  storageReady = false;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.message == "FS state failed" && probe.streams == 0 && probe.reboots == 0);
  resetProbe("3.1.14-custom", hashA);
  streamReady = false;
  pullOtaRunUpdate(pullOtaMakeTargetVersion(3, 1, 14));
  assert(probe.streams == 1 && probe.clears == 1 && probe.reboots == 0);
}

int main() {
  testLegacyDowngradeCompatibility();
  testReleaseSelection();
  testOfficialToSameVersionCustom();
  testCustomCombinationIdentity();
  testAssignmentGuards();
  testCustomToSameVersionOfficial();
  testOfficialRunGuards();
}
'''.replace("@SELECTION@", selection).replace("@CUSTOM@", custom)
    for marker, signature in (
        ("@INSTALL@", "bool pullOtaInstall("),
        ("@NEWER@", "bool pullOtaHasNewerRelease("),
        ("@TARGET@", "bool pullOtaFindTargetRelease("),
        ("@RUN@", "void pullOtaRunUpdate("),
    ):
        source = source.replace(marker, block_after(ota, signature))
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp, binary = root / "selection.cpp", root / "selection.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Same-version official/custom OTA selection, installation, and assignment runtime tests passed")


if __name__ == "__main__":
    main()
