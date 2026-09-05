#ifndef CUSTOM_BUILD_OTA_H
#define CUSTOM_BUILD_OTA_H

#if HDS_FEATURE_PULL_OTA

#include <Preferences.h>
#include <esp_system.h>

#if __has_include("custom_ota_public_key.h")
#include "custom_ota_public_key.h"
#endif

#ifndef HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM
#define HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM ""
#endif

#ifndef HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM
#define HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM ""
#endif

#ifndef HDS_CUSTOM_BUILD_COMBINATION_HASH
#define HDS_CUSTOM_BUILD_COMBINATION_HASH ""
#endif

#ifndef HDS_CUSTOM_BUILD_SERVICE_URL
#define HDS_CUSTOM_BUILD_SERVICE_URL "https://openscale-custom-builds.odevstudio.workers.dev"
#endif

static const char HDS_CUSTOM_BUILD_NVS_NAMESPACE[] = "ota_custom";
static const unsigned long HDS_CUSTOM_BUILD_SCREEN_TIMEOUT_MS = 30000;
static const unsigned long HDS_CUSTOM_BUILD_HOLD_MS = 1200;
static const size_t HDS_CUSTOM_BUILD_RESPONSE_MAX_BYTES = 2048;

struct CustomBuildAssignment {
  bool linked = false;
  bool identityRejected = false;
  String state = "";
  String combinationHash = "";
};

void customBuildRandomHex(char *output, size_t byteCount) {
  uint8_t bytes[32];
  if (output == nullptr || byteCount > sizeof(bytes)) return;
  esp_fill_random(bytes, byteCount);
  for (size_t index = 0; index < byteCount; index++) {
    snprintf(output + index * 2, 3, "%02x", bytes[index]);
  }
  output[byteCount * 2] = '\0';
}

bool customBuildHexValueValid(const String &value, size_t length) {
  if (value.length() != length) return false;
  for (size_t index = 0; index < length; index++) {
    const char character = value.charAt(index);
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) return false;
  }
  return true;
}

void customBuildHashPrefix(const String &hash, char output[9]) {
  if (!customBuildHexValueValid(hash, 64)) {
    output[0] = '\0';
    return;
  }
  for (size_t index = 0; index < 8; index++) {
    const char character = hash.charAt(index);
    output[index] = character >= 'a' && character <= 'f'
        ? character - 'a' + 'A' : character;
  }
  output[8] = '\0';
}

bool customBuildLoadCredentials(String &deviceId, String &deviceSecret, bool *pairInitialized = nullptr) {
  Preferences preferences;
  if (!preferences.begin(HDS_CUSTOM_BUILD_NVS_NAMESPACE, false)) return false;
  deviceId = preferences.getString("device_id", "");
  deviceSecret = preferences.getString("device_secret", "");
  if (!customBuildHexValueValid(deviceId, 32) || !customBuildHexValueValid(deviceSecret, 64)) {
    char newDeviceId[33];
    char newDeviceSecret[65];
    customBuildRandomHex(newDeviceId, 16);
    customBuildRandomHex(newDeviceSecret, 32);
    const bool stored = preferences.putBool("pair_init", false) == 1 &&
                        preferences.putString("device_id", newDeviceId) == 32 &&
                        preferences.putString("device_secret", newDeviceSecret) == 64;
    if (!stored) {
      preferences.remove("device_id");
      preferences.remove("device_secret");
      preferences.end();
      return false;
    }
    deviceId = newDeviceId;
    deviceSecret = newDeviceSecret;
  }
  if (pairInitialized != nullptr) *pairInitialized = preferences.getBool("pair_init", false);
  preferences.end();
  return true;
}

void customBuildSerialHint(char output[7]) {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(output, 7, "%06lX", (unsigned long)(mac & 0xFFFFFFUL));
}

uint32_t customBuildPairPin() {
  const uint32_t range = 1000000;
  const uint32_t limit = UINT32_MAX - UINT32_MAX % range;
  uint32_t value;
  do {
    value = esp_random();
  } while (value >= limit);
  return value % range;
}

bool customBuildRequest(
    const char *path,
    const char *method,
    const String &requestBody,
    String &responseBody,
    int *responseStatus = nullptr) {
  if (responseStatus != nullptr) *responseStatus = 0;
  String deviceId;
  String deviceSecret;
  if (!customBuildLoadCredentials(deviceId, deviceSecret)) return false;
  HTTPClient http;
  WiFiClientSecure client;
  const String url = String(HDS_CUSTOM_BUILD_SERVICE_URL) + path;
  if (!pullOtaBeginHttp(http, client, url)) return false;
  http.addHeader("Authorization", "Bearer " + deviceSecret);
  http.addHeader("X-OpenScale-Device-ID", deviceId);
  int status;
  if (strcmp(method, "POST") == 0) {
    http.addHeader("Content-Type", "application/json");
    status = http.POST(requestBody);
  } else {
    status = http.GET();
  }
  if (responseStatus != nullptr) *responseStatus = status;
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const bool read = pullOtaReadHttpBody(http, responseBody, HDS_CUSTOM_BUILD_RESPONSE_MAX_BYTES);
  http.end();
  return read;
}

bool customBuildCheckIn(
    const String &installedCombination,
    CustomBuildAssignment &assignment) {
  JsonDocument request;
  request["installed_combination"] = installedCombination.length() > 0
      ? installedCombination.c_str() : nullptr;
  request["firmware_version"] = pullOtaCurrentVersion();
  String requestBody;
  serializeJson(request, requestBody);
  String body;
  int status = 0;
  if (!customBuildRequest("/api/v1/device/check-in", "POST", requestBody, body, &status)) {
    assignment.identityRejected = status == HTTP_CODE_UNAUTHORIZED;
    return assignment.identityRejected;
  }
  JsonDocument document;
  if (deserializeJson(document, body)) return false;
  JsonObject root = document.as<JsonObject>();
  const char *state = root["state"] | "";
  const char *combinationHash = root["desired_combination"] | "";
  assignment.linked = root["linked"] | false;
  assignment.state = state;
  assignment.combinationHash = combinationHash;
  if (assignment.combinationHash.length() > 0 &&
      !customBuildHexValueValid(assignment.combinationHash, 64)) return false;
  return true;
}

bool customBuildRegisterPairCode(const char *pairCode) {
  JsonDocument document;
  document["pair_code"] = pairCode;
  String requestBody;
  serializeJson(document, requestBody);
  String responseBody;
  if (!customBuildRequest("/api/v1/device/pair", "POST", requestBody, responseBody)) return false;
  JsonDocument response;
  if (deserializeJson(response, responseBody)) return false;
  if (String(response["pair_code"] | "") != pairCode) return false;
  Preferences preferences;
  if (!preferences.begin(HDS_CUSTOM_BUILD_NVS_NAMESPACE, false)) return false;
  const bool stored = preferences.putBool("pair_init", true) == 1;
  preferences.end();
  return stored;
}

bool customBuildWaitForHold(uint8_t pin, unsigned long timeoutMs) {
  const unsigned long startedAt = millis();
  unsigned long heldSince = 0;
  while (millis() - startedAt < timeoutMs) {
    if (digitalRead(pin) == LOW) {
      if (heldSince == 0) heldSince = millis();
      if (millis() - heldSince >= HDS_CUSTOM_BUILD_HOLD_MS) return true;
    } else {
      heldSince = 0;
    }
    delay(20);
  }
  return false;
}

void customBuildWaitForDismiss(unsigned long timeoutMs) {
  pullOtaWaitForRelease(1000);
  const unsigned long startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (digitalRead(BUTTON_CIRCLE) == LOW || digitalRead(BUTTON_SQUARE) == LOW) {
      pullOtaWaitForRelease(1000);
      return;
    }
    delay(20);
  }
}

bool customBuildPairScale(bool requireConfirmation) {
  pullOtaWaitForRelease(1000);
  if (requireConfirmation) {
    pullOtaDraw("Custom Build", "Pair scale?", "Hold square");
    if (!customBuildWaitForHold(BUTTON_SQUARE, HDS_OTA_CONFIRM_TIMEOUT_MS)) {
      return pullOtaFail("Pairing cancelled");
    }
  }
  pullOtaDraw("Custom Build", "Creating code");
  if (!pullOtaEnsureWifi() || !pullOtaClockReady()) {
    return pullOtaFail("Network failed");
  }
  char serialHint[7];
  char pairCode[14];
  customBuildSerialHint(serialHint);
  snprintf(pairCode, sizeof(pairCode), "%s-%06lu", serialHint, (unsigned long)customBuildPairPin());
  if (!customBuildRegisterPairCode(pairCode)) return pullOtaFail("Pairing failed");
  pullOtaDraw("Pair code", pairCode, "Valid 12 hours");
  customBuildWaitForDismiss(HDS_CUSTOM_BUILD_SCREEN_TIMEOUT_MS);
  return true;
}

bool customBuildManifestUrl(const String &combinationHash, String &manifestUrl) {
  if (!customBuildHexValueValid(combinationHash, 64)) return false;
  manifestUrl = String(HDS_CUSTOM_BUILD_SERVICE_URL) + "/v1/" + combinationHash +
                "/ota-manifest.json";
  return true;
}

bool customBuildFetchManifestFile(const String &url, String &body) {
  if (!url.startsWith(String(HDS_CUSTOM_BUILD_SERVICE_URL) + "/v1/") ||
      !url.endsWith("/ota-manifest.json")) return false;
  HTTPClient http;
  WiFiClientSecure client;
  if (!pullOtaBeginHttp(http, client, url)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const bool read = pullOtaReadHttpBody(http, body, HDS_OTA_MANIFEST_MAX_BYTES);
  http.end();
  return read;
}

bool customBuildFetchManifestSignature(
    const String &manifestUrl,
    uint8_t *signature,
    size_t &signatureLength) {
  const String suffix = "/ota-manifest.json";
  if (!manifestUrl.endsWith(suffix)) return false;
  const String signatureUrl = manifestUrl.substring(0, manifestUrl.length() - suffix.length()) +
                              "/ota-manifest.sig";
  HTTPClient http;
  WiFiClientSecure client;
  if (!pullOtaBeginHttp(http, client, signatureUrl)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const bool read = pullOtaReadHttpBytes(
      http, signature, HDS_OTA_MANIFEST_SIGNATURE_MAX_BYTES, signatureLength);
  http.end();
  return read;
}

bool customBuildFetchManifest(
    const String &combinationHash,
    PullOtaManifest &manifest) {
  if (strlen(HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM) == 0 ||
      strlen(HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM) == 0) return false;
  String manifestUrl;
  String body;
  if (!customBuildManifestUrl(combinationHash, manifestUrl) ||
      !customBuildFetchManifestFile(manifestUrl, body)) return false;
  uint8_t signature[HDS_OTA_MANIFEST_SIGNATURE_MAX_BYTES];
  size_t signatureLength = 0;
  if (!customBuildFetchManifestSignature(manifestUrl, signature, signatureLength)) return false;
  const char *const keys[] = {
      HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_1_PEM,
      HDS_CUSTOM_OTA_MANIFEST_PUBLIC_KEY_2_PEM,
  };
  if (!pullOtaVerifyManifestSignatureWithKeys(
          body, signature, signatureLength, keys, sizeof(keys) / sizeof(keys[0]))) return false;
  JsonDocument document;
  if (deserializeJson(document, body)) return false;
  JsonObject root = document.as<JsonObject>();
  if (!(root["custom_build"] | false) ||
      String(root["combination_hash"] | "") != combinationHash) return false;
  const String assetPrefix = String(HDS_CUSTOM_BUILD_SERVICE_URL) + "/v1/" +
                             combinationHash + "/";
  return pullOtaParseManifestObject(root, manifest, assetPrefix.c_str(), false);
}

bool customBuildShowRelink(const char *line1, const char *line2) {
  pullOtaWaitForRelease(1000);
  pullOtaDraw(line1, line2, "Hold O relink");
  if (!customBuildWaitForHold(BUTTON_CIRCLE, HDS_CUSTOM_BUILD_SCREEN_TIMEOUT_MS)) return true;
  return customBuildPairScale(false);
}

bool customBuildConfirmInstall(
    const PullOtaManifest &manifest,
    const String &combinationHash,
    bool &relink) {
  char hashPrefix[9];
  customBuildHashPrefix(combinationHash, hashPrefix);
  pullOtaWaitForRelease(1000);
  pullOtaDraw(manifest.version.c_str(), hashPrefix, "Sq install O relink");
  const unsigned long startedAt = millis();
  unsigned long squareHeldSince = 0;
  unsigned long circleHeldSince = 0;
  while (millis() - startedAt < HDS_OTA_CONFIRM_TIMEOUT_MS) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      if (squareHeldSince == 0) squareHeldSince = millis();
      if (millis() - squareHeldSince >= HDS_CUSTOM_BUILD_HOLD_MS) return true;
    } else {
      squareHeldSince = 0;
    }
    if (digitalRead(BUTTON_CIRCLE) == LOW) {
      if (circleHeldSince == 0) circleHeldSince = millis();
      if (millis() - circleHeldSince >= HDS_CUSTOM_BUILD_HOLD_MS) {
        relink = true;
        return false;
      }
    } else {
      circleHeldSince = 0;
    }
    delay(20);
  }
  return false;
}

void customBuildRun() {
  String deviceId;
  String deviceSecret;
  bool pairInitialized = false;
  if (!customBuildLoadCredentials(deviceId, deviceSecret, &pairInitialized)) {
    pullOtaFail("Storage failed");
    return;
  }
  if (!pairInitialized) {
    customBuildPairScale(true);
    return;
  }
  pullOtaDraw("Custom Build", "Checking");
  if (!pullOtaEnsureWifi() || !pullOtaClockReady()) {
    pullOtaFail("Network failed");
    return;
  }
  const String installedCombination = pullOtaCurrentCombinationHash();
  CustomBuildAssignment assignment;
  if (!customBuildCheckIn(installedCombination, assignment)) {
    pullOtaFail("Service failed");
    return;
  }
  if (assignment.identityRejected) {
    customBuildShowRelink("Custom Build", "Device rejected");
    return;
  }
  if (!assignment.linked) {
    customBuildPairScale(true);
    return;
  }
  if (assignment.combinationHash.length() == 0) {
    customBuildShowRelink("Custom Build", "No build assigned");
    return;
  }
  if (assignment.combinationHash == installedCombination) {
    char hashPrefix[9];
    customBuildHashPrefix(installedCombination, hashPrefix);
    customBuildShowRelink("Already installed", hashPrefix);
    return;
  }
  if (assignment.state == "queued" || assignment.state == "building") {
    customBuildShowRelink("Custom Build", "Build preparing");
    return;
  }
  if (assignment.state != "ready") {
    customBuildShowRelink("Custom Build", "Build unavailable");
    return;
  }
  PullOtaManifest manifest;
  PullOtaManifest rollbackManifest;
  if (!customBuildFetchManifest(assignment.combinationHash, manifest)) {
    pullOtaFail("Signature failed");
    return;
  }
  bool relink = false;
  if (!customBuildConfirmInstall(manifest, assignment.combinationHash, relink)) {
    if (relink) customBuildPairScale(false);
    return;
  }
  const String rollbackCombinationHash = installedCombination;
  const bool rollbackFound = rollbackCombinationHash.length() > 0
      ? customBuildFetchManifest(rollbackCombinationHash, rollbackManifest)
      : pullOtaFetchCurrentReleaseManifest(rollbackManifest);
  if (!rollbackFound) {
    pullOtaFail("Rollback missing");
    return;
  }
  pullOtaInstall(
      manifest,
      rollbackManifest,
      assignment.combinationHash,
      rollbackCombinationHash);
}

void customBuildTask(void *args) {
  (void)args;
  const unsigned long pauseStartedAt = millis();
  while (!otaRuntimeIsPaused() &&
         millis() - pauseStartedAt < OTA_RUNTIME_PAUSE_TIMEOUT_MS) {
    delay(1);
  }
  if (otaRuntimeIsPaused()) customBuildRun();
  else pullOtaFail("OTA runtime pause failed");
  portENTER_CRITICAL(&wsPendingMux);
  const bool restartPending = (wsPendingMask & WSP_OTA_RESET) != 0;
  portEXIT_CRITICAL(&wsPendingMux);
  if (!restartPending) {
    b_ota = false;
    b_pullOtaRunning = false;
  }
  vTaskDelete(NULL);
}

void customBuildMenu() {
  if (b_pullOtaRunning || b_ota) return;
  setOtaRuntimePaused(false);
  b_pullOtaRunning = true;
  b_ota = true;
  const BaseType_t started = xTaskCreate(
      customBuildTask,
      "Custom OTA",
      HDS_OTA_TASK_STACK_BYTES,
      NULL,
      1,
      NULL);
  if (started != pdPASS) {
    b_pullOtaRunning = false;
    b_ota = false;
    pullOtaFail("OTA task failed");
  }
}

#endif
#endif
