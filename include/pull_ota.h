#ifndef PULL_OTA_H
#define PULL_OTA_H

#ifdef WIFIOTA

#include "config.h"
#include "display.h"
#include "parameter.h"
#include "pull_ota_version.h"
#include "webserver.h"
#include "wifi_ota.h"
#include "wifi_setup.h"
#if __has_include("ota_public_key.h")
#include "ota_public_key.h"
#endif
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_partition.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <string.h>
#include <time.h>

void setupWebsocketEvents();
void wifi_init();
void hdsOtaRollbackMarkValid();

#ifndef HDS_OTA_MANIFEST_URL
#define HDS_OTA_MANIFEST_URL "https://github.com/decentespresso/openscale/releases/latest/download/manifest.json"
#endif

#ifndef HDS_OTA_ASSET_URL_PREFIX
#define HDS_OTA_ASSET_URL_PREFIX "https://github.com/decentespresso/openscale/releases/download/"
#endif

#ifndef HDS_OTA_MANIFEST_PUBLIC_KEY_PEM
#define HDS_OTA_MANIFEST_PUBLIC_KEY_PEM ""
#endif

static const char HDS_OTA_CA_CERTS[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQswCQYDVQQGEwJH
QjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2
ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcNMjEwMzIyMDAwMDAwWhcNNDYwMzIxMjM1OTU5
WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0
aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUr
gQQAIgNiAAR2+pmpbiDt+dd34wc7qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0
NSt3zn8gj1KjAIns1aeibVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6OjQjBAMB0GA1Ud
DgQWBBTRItpMWfFLXyY4qp3W7usNw/upYTAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB
/zAKBggqhkjOPQQDAwNnADBkAjAn7qRaqCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCkT0RH
lAFWovgzJQxC36oCMB3q4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgED1cf5kDW21U
SAGKcw==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAwTzELMAkGA1UE
BhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdyb3VwMRUwEwYDVQQD
EwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQG
EwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMT
DElTUkcgUm9vdCBYMTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54r
Vygch77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+0TM8ukj1
3Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6UA5/TR5d8mUgjU+g4rk8K
b4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sWT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCN
Aymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyHB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ
4Q7e2RCOFvu396j3x+UCB5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf
1b0SHzUvKBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWnOlFu
hjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTnjh8BCNAw1FtxNrQH
usEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbwqHyGO0aoSCqI3Haadr8faqU9GY/r
OPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CIrU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4G
A1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY
9umbbjANBgkqhkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ3BebYhtF8GaV
0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KKNFtY2PwByVS5uCbMiogziUwt
hDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJw
TdwJx4nLCgdNbOhdjsnvzqvHu7UrTkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nx
e5AW0wdeRlN8NwdCjNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZA
JzVcoyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq4RgqsahD
YVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPAmRGunUHBcnWEvgJBQl9n
JEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57demyPxgcYxn/eR44/KJ4EBs+lVDR3veyJ
m+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";

static const size_t HDS_OTA_MANIFEST_MAX_BYTES = 32768;
static const size_t HDS_OTA_MANIFEST_SIGNATURE_MAX_BYTES = 1024;
static const size_t HDS_OTA_BUFFER_BYTES = 1024;
static const unsigned long HDS_OTA_WIFI_TIMEOUT_MS = 45000;
static const unsigned long HDS_OTA_PENDING_WIFI_TIMEOUT_MS = 10000;
static const unsigned long HDS_OTA_HTTP_TIMEOUT_MS = 20000;
static const unsigned long HDS_OTA_CLOCK_TIMEOUT_MS = 8000;
static const unsigned long HDS_OTA_CONFIRM_TIMEOUT_MS = 15000;
static const unsigned long HDS_OTA_CONFIRM_HOLD_MS = 1200;
static const unsigned long HDS_OTA_PICK_TIMEOUT_MS = 30000;
static const uint32_t HDS_OTA_TASK_STACK_BYTES = 24576;
static const uint8_t HDS_OTA_MAX_RELEASE_CHOICES = 16;
static const char *HDS_OTA_CHIP = "esp32s3";
static const char *HDS_OTA_ENVIRONMENT = "esp32s3";
static const char *HDS_OTA_PARTITION_SCHEMA = "esp32s3-default-8mb-ota-spiffs-1536k";
static const char *HDS_OTA_FS_PARTITION_LABEL = "spiffs";
static const uint32_t HDS_OTA_FLASH_SIZE = 8388608;
static const uint32_t HDS_OTA_APP_PARTITION_MIN_SIZE = 3342336;
static const uint32_t HDS_OTA_FS_PARTITION_SIZE = 1572864;
static const uint32_t HDS_OTA_FS_SCHEMA = 1;
static const char *HDS_OTA_MIN_INSTALL_VERSION = "3.1.13";

struct PullOtaAsset {
  bool present = false;
  bool required = false;
  String url = "";
  size_t size = 0;
  String sha256 = "";
};

struct PullOtaManifest {
  String model = "";
  String version = "";
  String minFrom = "";
  String releaseNotesUrl = "";
  String pcb = "";
  String chip = "";
  String environment = "";
  String partitionSchema = "";
  String fsPartitionLabel = "";
  uint32_t flashSize = 0;
  uint32_t appPartitionMinSize = 0;
  uint32_t fsPartitionSize = 0;
  uint32_t fsSchema = 0;
  PullOtaAsset firmware;
  PullOtaAsset littlefs;
};

struct PullOtaReleaseList {
  PullOtaManifest releases[HDS_OTA_MAX_RELEASE_CHOICES];
  uint8_t count = 0;
};

struct PullOtaPendingLittleFs {
  bool present = false;
  bool restore = false;
  bool restoreAttempted = false;
  bool filesystemDirty = false;
  uint8_t targetAttempts = 0;
  String version = "";
  String rollbackVersion = "";
  String fsPartitionLabel = "";
  uint32_t fsPartitionSize = 0;
  uint32_t fsSchema = 0;
  PullOtaAsset asset;
  PullOtaAsset rollbackAsset;
};

struct PullOtaHash {
  mbedtls_md_context_t context;
  const mbedtls_md_info_t *info = nullptr;
  bool active = false;
};

void pullOtaDraw(const char *line1, const char *line2 = "", const char *line3 = "") {
  if (b_screenFlipped)
    u8g2.setDisplayRotation(U8G2_R0);
  else
    u8g2.setDisplayRotation(U8G2_R2);
  u8g2.setFont(FONT_S);
  u8g2.firstPage();
  do {
    if (line1 && line1[0] != '\0') u8g2.drawUTF8(AC(line1), 14, line1);
    if (line2 && line2[0] != '\0') u8g2.drawUTF8(AC(line2), 36, line2);
    if (line3 && line3[0] != '\0') u8g2.drawUTF8(AC(line3), 58, line3);
  } while (u8g2.nextPage());
}

void pullOtaDrawProgress(const char *label, uint8_t percent) {
  char percentLine[24];
  snprintf(percentLine, sizeof(percentLine), "%u%%", percent);
  pullOtaDraw(label, percentLine);
}

bool pullOtaFail(const char *line1, const char *line2 = "") {
  Serial.printf("[pull-ota] error %s %s\n", line1, line2);
  pullOtaDraw(line1, line2);
  delay(2000);
  b_ota = false;
  return false;
}

String pullOtaCurrentVersion() {
  String version = FIRMWARE_VER;
  int colon = version.indexOf(':');
  if (colon >= 0) {
    version = version.substring(colon + 1);
  }
  version.trim();
  return version;
}

bool pullOtaParseVersionTriplet(String version, uint16_t parts[3]) {
  version.trim();
  PullOtaVersionTriplet parsed;
  if (!pullOtaParseVersionPrefix(version.c_str(), parsed) || *parsed.suffix != '\0') {
    return false;
  }
  for (uint8_t part = 0; part < 3; part++) parts[part] = parsed.parts[part];
  return true;
}

bool pullOtaVersionLooksStable(const String &version) {
  String trimmed = version;
  trimmed.trim();
  return pullOtaVersionIsStable(trimmed.c_str());
}

int pullOtaCompareVersions(const String &left, const String &right) {
  String leftTrimmed = left;
  String rightTrimmed = right;
  leftTrimmed.trim();
  rightTrimmed.trim();
  return pullOtaCompareVersionPrefixes(leftTrimmed.c_str(), rightTrimmed.c_str());
}

bool pullOtaUrlAllowed(const String &url) {
  return url.startsWith(HDS_OTA_ASSET_URL_PREFIX);
}

bool pullOtaShaLooksValid(String value) {
  value.trim();
  if (value.length() != 64) {
    return false;
  }
  for (int i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    bool digit = c >= '0' && c <= '9';
    bool lower = c >= 'a' && c <= 'f';
    bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !upper) {
      return false;
    }
  }
  return true;
}

bool pullOtaParseAsset(JsonVariant variant, PullOtaAsset &asset, bool requiredDefault) {
  JsonObject object = variant.as<JsonObject>();
  if (object.isNull()) {
    return false;
  }
  const char *url = object["url"] | "";
  const char *sha = object["sha256"] | "";
  unsigned long size = object["size"] | 0UL;
  String shaValue = sha;
  shaValue.trim();
  shaValue.toLowerCase();
  if (url[0] == '\0' || size == 0 || !pullOtaShaLooksValid(shaValue)) {
    return false;
  }
  asset.present = true;
  asset.required = object["required"] | requiredDefault;
  asset.url = url;
  asset.size = (size_t)size;
  asset.sha256 = shaValue;
  return pullOtaUrlAllowed(asset.url);
}

bool pullOtaManifestCompatible(const PullOtaManifest &manifest) {
  if (manifest.chip != HDS_OTA_CHIP) {
    return false;
  }
  if (manifest.environment != HDS_OTA_ENVIRONMENT) {
    return false;
  }
  if (manifest.partitionSchema != HDS_OTA_PARTITION_SCHEMA) {
    return false;
  }
  if (ESP.getFlashChipSize() < manifest.flashSize) {
    return false;
  }
  if (ESP.getFreeSketchSpace() < manifest.appPartitionMinSize) {
    return false;
  }
  if (manifest.firmware.size > ESP.getFreeSketchSpace()) {
    return false;
  }
  if (manifest.fsPartitionLabel != HDS_OTA_FS_PARTITION_LABEL) {
    return false;
  }
  const esp_partition_t *fsPartition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      HDS_OTA_FS_PARTITION_LABEL);
  if (fsPartition == nullptr || fsPartition->size != manifest.fsPartitionSize) {
    return false;
  }
  if (manifest.littlefs.present && manifest.littlefs.size != manifest.fsPartitionSize) {
    return false;
  }
  return manifest.fsSchema == HDS_OTA_FS_SCHEMA;
}

bool pullOtaReleaseCanRunFromCurrent(const PullOtaManifest &manifest) {
  if (pullOtaCompareVersions(manifest.version, HDS_OTA_MIN_INSTALL_VERSION) < 0) {
    return false;
  }
  String currentVersion = pullOtaCurrentVersion();
  if (manifest.minFrom.length() > 0 &&
      pullOtaCompareVersions(currentVersion, manifest.minFrom) < 0) {
    return false;
  }
  return true;
}

bool pullOtaParseManifestObject(JsonObject root, PullOtaManifest &manifest) {
  const char *model = root["model"] | "";
  const char *version = root["version"] | "";
  const char *minFrom = root["min_from"] | "";
  const char *releaseNotesUrl = root["release_notes_url"] | "";
  const char *pcb = root["pcb"] | "";
  const char *chip = root["chip"] | "";
  const char *environment = root["environment"] | "";
  const char *partitionSchema = root["partition_schema"] | "";
  const char *fsPartitionLabel = root["fs_partition_label"] | "";
  unsigned long flashSize = root["flash_size"] | 0UL;
  unsigned long appPartitionMinSize = root["app_partition_min_size"] | 0UL;
  unsigned long fsPartitionSize = root["fs_partition_size"] | 0UL;
  unsigned long fsSchema = root["fs_schema"] | 0UL;
  if (String(model) != "hds" || !pullOtaVersionLooksStable(version)) {
    return false;
  }
  if (minFrom[0] != '\0' && !pullOtaVersionLooksStable(minFrom)) {
    return false;
  }
  if (pcb[0] != '\0' && String(pcb) != String(PCB_VER)) {
    return false;
  }
  if (chip[0] == '\0' || environment[0] == '\0' || partitionSchema[0] == '\0' ||
      fsPartitionLabel[0] == '\0' || flashSize == 0 ||
      appPartitionMinSize == 0 || fsPartitionSize == 0 || fsSchema == 0) {
    return false;
  }
  manifest.model = model;
  manifest.version = version;
  manifest.minFrom = minFrom;
  manifest.releaseNotesUrl = releaseNotesUrl;
  manifest.pcb = pcb;
  manifest.chip = chip;
  manifest.environment = environment;
  manifest.partitionSchema = partitionSchema;
  manifest.fsPartitionLabel = fsPartitionLabel;
  manifest.flashSize = (uint32_t)flashSize;
  manifest.appPartitionMinSize = (uint32_t)appPartitionMinSize;
  manifest.fsPartitionSize = (uint32_t)fsPartitionSize;
  manifest.fsSchema = (uint32_t)fsSchema;
  if (!pullOtaParseAsset(root["firmware"], manifest.firmware, true)) {
    return false;
  }
  if (!root["littlefs"].isNull() &&
      !pullOtaParseAsset(root["littlefs"], manifest.littlefs, false)) {
    return false;
  }
  return pullOtaManifestCompatible(manifest);
}

bool pullOtaSameRelease(const PullOtaManifest &left, const PullOtaManifest &right) {
  return left.model == right.model &&
         left.pcb == right.pcb &&
         left.version == right.version &&
         left.chip == right.chip &&
         left.environment == right.environment;
}

void pullOtaAddRelease(PullOtaReleaseList &list, const PullOtaManifest &candidate) {
  for (uint8_t i = 0; i < list.count; i++) {
    if (pullOtaSameRelease(list.releases[i], candidate)) {
      return;
    }
  }
  uint8_t insertAt = 0;
  while (insertAt < list.count &&
         pullOtaCompareVersions(list.releases[insertAt].version, candidate.version) > 0) {
    insertAt++;
  }
  if (list.count == HDS_OTA_MAX_RELEASE_CHOICES && insertAt >= HDS_OTA_MAX_RELEASE_CHOICES) {
    return;
  }
  if (list.count < HDS_OTA_MAX_RELEASE_CHOICES) {
    list.count++;
  }
  for (int i = (int)list.count - 1; i > (int)insertAt; i--) {
    list.releases[i] = list.releases[i - 1];
  }
  list.releases[insertAt] = candidate;
}

bool pullOtaAddParsedRelease(JsonObject releaseObject, PullOtaReleaseList &list) {
  PullOtaManifest candidate;
  if (!pullOtaParseManifestObject(releaseObject, candidate)) {
    return false;
  }
  if (!pullOtaReleaseCanRunFromCurrent(candidate)) {
    return true;
  }
  pullOtaAddRelease(list, candidate);
  return true;
}

void pullOtaBuildSelectableReleases(
    const PullOtaReleaseList &catalog,
    PullOtaReleaseList &selectable) {
  String currentVersion = pullOtaCurrentVersion();
  for (uint8_t i = 0; i < catalog.count; i++) {
    int currentCompare = pullOtaCompareVersions(catalog.releases[i].version, currentVersion);
    if (currentCompare != 0) {
      pullOtaAddRelease(selectable, catalog.releases[i]);
    }
  }
}

bool pullOtaHasNewerRelease(const PullOtaReleaseList &list) {
  String currentVersion = pullOtaCurrentVersion();
  for (uint8_t i = 0; i < list.count; i++) {
    if (pullOtaCompareVersions(list.releases[i].version, currentVersion) > 0) {
      return true;
    }
  }
  return false;
}

bool pullOtaFindCurrentRelease(
    const PullOtaReleaseList &catalog,
    PullOtaManifest &current) {
  String currentVersion = pullOtaCurrentVersion();
  for (uint8_t i = 0; i < catalog.count; i++) {
    if (pullOtaCompareVersions(catalog.releases[i].version, currentVersion) == 0 &&
        catalog.releases[i].littlefs.present &&
        catalog.releases[i].littlefs.required) {
      current = catalog.releases[i];
      return true;
    }
  }
  return false;
}

bool pullOtaSelectReleases(JsonArray releases, PullOtaReleaseList &list) {
  for (JsonVariant releaseVariant : releases) {
    JsonObject releaseObject = releaseVariant.as<JsonObject>();
    if (releaseObject.isNull()) {
      continue;
    }
    pullOtaAddParsedRelease(releaseObject, list);
  }
  return true;
}

bool pullOtaSelectReleases(JsonObject root, PullOtaReleaseList &list) {
  if (!root["releases"].isNull()) {
    JsonArray releases = root["releases"].as<JsonArray>();
    if (releases.isNull()) {
      return false;
    }
    return pullOtaSelectReleases(releases, list);
  }
  return pullOtaAddParsedRelease(root, list);
}

bool pullOtaSelectRelease(JsonArray releases, PullOtaManifest &manifest) {
  PullOtaReleaseList list;
  if (!pullOtaSelectReleases(releases, list) || list.count == 0) {
    return false;
  }
  manifest = list.releases[0];
  return true;
}

bool pullOtaSelectRelease(JsonObject root, PullOtaManifest &manifest) {
  PullOtaReleaseList list;
  if (!pullOtaSelectReleases(root, list) || list.count == 0) {
    return false;
  }
  manifest = list.releases[0];
  return true;
}

bool pullOtaParseManifest(const String &body, PullOtaReleaseList &list) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    return false;
  }
  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    return false;
  }
  return pullOtaSelectReleases(root, list);
}

bool pullOtaParseManifest(const String &body, PullOtaManifest &manifest) {
  PullOtaReleaseList list;
  if (!pullOtaParseManifest(body, list) || list.count == 0) {
    return false;
  }
  manifest = list.releases[0];
  return true;
}

bool pullOtaHashBegin(PullOtaHash &hash) {
  mbedtls_md_init(&hash.context);
  hash.info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (hash.info == nullptr) {
    return false;
  }
  if (mbedtls_md_setup(&hash.context, hash.info, 0) != 0) {
    mbedtls_md_free(&hash.context);
    return false;
  }
  if (mbedtls_md_starts(&hash.context) != 0) {
    mbedtls_md_free(&hash.context);
    return false;
  }
  hash.active = true;
  return true;
}

bool pullOtaHashUpdate(PullOtaHash &hash, const uint8_t *data, size_t len) {
  return hash.active && mbedtls_md_update(&hash.context, data, len) == 0;
}

bool pullOtaHashFinish(PullOtaHash &hash, uint8_t digest[32]) {
  if (!hash.active) {
    return false;
  }
  bool ok = mbedtls_md_finish(&hash.context, digest) == 0;
  mbedtls_md_free(&hash.context);
  hash.active = false;
  return ok;
}

String pullOtaHexDigest(const uint8_t digest[32]) {
  char text[65];
  for (uint8_t i = 0; i < 32; i++) {
    snprintf(text + i * 2, 3, "%02x", digest[i]);
  }
  text[64] = '\0';
  return String(text);
}

bool sha256Matches(const uint8_t digest[32], const String &expected) {
  return pullOtaHexDigest(digest) == expected;
}

bool pullOtaPublicKeyConfigured() {
  return strlen(HDS_OTA_MANIFEST_PUBLIC_KEY_PEM) > 0;
}

bool pullOtaSha256String(const String &body, uint8_t digest[32]) {
  PullOtaHash hash;
  if (!pullOtaHashBegin(hash)) {
    return false;
  }
  bool ok = pullOtaHashUpdate(hash, (const uint8_t *)body.c_str(), body.length());
  return pullOtaHashFinish(hash, digest) && ok;
}

bool pullOtaVerifyManifestSignature(
    const String &body,
    const uint8_t *signature,
    size_t signatureLen) {
  if (!pullOtaPublicKeyConfigured() || signature == nullptr || signatureLen == 0) {
    return false;
  }
  uint8_t digest[32];
  if (!pullOtaSha256String(body, digest)) {
    return false;
  }
  mbedtls_pk_context publicKey;
  mbedtls_pk_init(&publicKey);
  const unsigned char *key = (const unsigned char *)HDS_OTA_MANIFEST_PUBLIC_KEY_PEM;
  int parsed = mbedtls_pk_parse_public_key(
      &publicKey, key, strlen(HDS_OTA_MANIFEST_PUBLIC_KEY_PEM) + 1);
  if (parsed != 0) {
    mbedtls_pk_free(&publicKey);
    return false;
  }
  int verified = mbedtls_pk_verify(
      &publicKey, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, signatureLen);
  mbedtls_pk_free(&publicKey);
  return verified == 0;
}

bool pullOtaPartitionShaMatches(const PullOtaAsset &asset) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      HDS_OTA_FS_PARTITION_LABEL);
  if (partition == nullptr || asset.size > partition->size) {
    return false;
  }
  PullOtaHash hash;
  if (!pullOtaHashBegin(hash)) {
    return false;
  }
  uint8_t buffer[HDS_OTA_BUFFER_BYTES];
  size_t checked = 0;
  bool ok = true;
  while (checked < asset.size) {
    size_t count = asset.size - checked;
    if (count > sizeof(buffer)) {
      count = sizeof(buffer);
    }
    if (esp_partition_read(partition, checked, buffer, count) != ESP_OK ||
        !pullOtaHashUpdate(hash, buffer, count)) {
      ok = false;
      break;
    }
    checked += count;
  }
  uint8_t digest[32];
  bool hashFinished = pullOtaHashFinish(hash, digest);
  return ok && hashFinished && checked == asset.size &&
         sha256Matches(digest, asset.sha256);
}

bool pullOtaLittleFsPartitionSizeMatches(uint32_t expectedSize) {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
      HDS_OTA_FS_PARTITION_LABEL);
  return partition != nullptr && partition->size == expectedSize;
}


bool pullOtaClockIsReady() {
  return time(nullptr) > 1700000000;
}

void pullOtaStartClockSync() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

bool pullOtaClockReady() {
  if (pullOtaClockIsReady()) {
    return true;
  }
  pullOtaStartClockSync();
  unsigned long startedAt = millis();
  while (millis() - startedAt < HDS_OTA_CLOCK_TIMEOUT_MS) {
    if (pullOtaClockIsReady()) {
      return true;
    }
    delay(200);
  }
  return false;
}

bool pullOtaBeginHttp(HTTPClient &http, WiFiClientSecure &client, const String &url) {
  if (!url.startsWith("https://")) {
    return false;
  }
  client.setCACert(HDS_OTA_CA_CERTS);
  client.setTimeout(HDS_OTA_HTTP_TIMEOUT_MS / 1000);
  http.setTimeout(HDS_OTA_HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  return http.begin(client, url);
}

bool pullOtaReadHttpBody(HTTPClient &http, String &body, size_t maxBytes) {
  int contentLength = http.getSize();
  if (contentLength > 0 && (size_t)contentLength > maxBytes) {
    return false;
  }
  if (contentLength > 0) {
    body.reserve(contentLength);
  } else {
    body.reserve(1024);
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[256];
  size_t total = 0;
  unsigned long lastRead = millis();
  while (http.connected() || stream->available()) {
    int available = stream->available();
    if (available > 0) {
      size_t request = (size_t)available;
      if (request > sizeof(buffer)) request = sizeof(buffer);
      size_t count = stream->readBytes(buffer, request);
      if (count == 0) {
        return false;
      }
      total += count;
      if (total > maxBytes) {
        return false;
      }
      body.concat((const char *)buffer, count);
      lastRead = millis();
      if (contentLength > 0 && total >= (size_t)contentLength) {
        break;
      }
    } else {
      if (millis() - lastRead > HDS_OTA_HTTP_TIMEOUT_MS) {
        return false;
      }
      delay(10);
    }
  }
  return total > 0 && (contentLength <= 0 || total == (size_t)contentLength);
}

bool pullOtaReadHttpBytes(
    HTTPClient &http,
    uint8_t *buffer,
    size_t maxBytes,
    size_t &length) {
  length = 0;
  int contentLength = http.getSize();
  if (contentLength > 0 && (size_t)contentLength > maxBytes) {
    return false;
  }
  WiFiClient *stream = http.getStreamPtr();
  unsigned long lastRead = millis();
  while (http.connected() || stream->available()) {
    int available = stream->available();
    if (available > 0) {
      size_t request = (size_t)available;
      if (length + request > maxBytes) {
        return false;
      }
      size_t count = stream->readBytes(buffer + length, request);
      if (count == 0) {
        return false;
      }
      length += count;
      lastRead = millis();
      if (contentLength > 0 && length >= (size_t)contentLength) {
        break;
      }
    } else {
      if (millis() - lastRead > HDS_OTA_HTTP_TIMEOUT_MS) {
        return false;
      }
      delay(10);
    }
  }
  return length > 0 && (contentLength <= 0 || length == (size_t)contentLength);
}

bool pullOtaFetchManifest(String &body) {
  HTTPClient http;
  WiFiClientSecure client;
  if (!pullOtaBeginHttp(http, client, HDS_OTA_MANIFEST_URL)) {
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  bool ok = pullOtaReadHttpBody(http, body, HDS_OTA_MANIFEST_MAX_BYTES);
  http.end();
  return ok;
}

String pullOtaManifestSignatureUrl() {
  String manifestUrl = HDS_OTA_MANIFEST_URL;
  String manifestName = "manifest.json";
  if (!manifestUrl.endsWith(manifestName)) {
    return "";
  }
  return manifestUrl.substring(0, manifestUrl.length() - manifestName.length()) +
         "manifest.sig";
}

bool pullOtaFetchManifestSignature(uint8_t *signature, size_t &signatureLen) {
  String signatureUrl = pullOtaManifestSignatureUrl();
  if (signatureUrl.length() == 0) {
    return false;
  }
  HTTPClient http;
  WiFiClientSecure client;
  if (!pullOtaBeginHttp(http, client, signatureUrl)) {
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  bool ok = pullOtaReadHttpBytes(
      http, signature, HDS_OTA_MANIFEST_SIGNATURE_MAX_BYTES, signatureLen);
  http.end();
  return ok;
}

bool pullOtaFetchSignedManifest(String &body) {
  if (!pullOtaFetchManifest(body)) {
    return false;
  }
  uint8_t signature[HDS_OTA_MANIFEST_SIGNATURE_MAX_BYTES];
  size_t signatureLen = 0;
  if (!pullOtaFetchManifestSignature(signature, signatureLen)) {
    return false;
  }
  return pullOtaVerifyManifestSignature(body, signature, signatureLen);
}

bool pullOtaEnsureWifi(unsigned long timeoutMs = HDS_OTA_WIFI_TIMEOUT_MS) {
  if (!wifiCredentialsSaved()) {
    return pullOtaFail("No saved WiFi", "Use setup first");
  }
  if (!b_wifiEnabled) {
    pullOtaDraw("Starting WiFi");
    b_wifiOnBoot = true;
    wifi_init();
  }
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    wifiSupervise();
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    return pullOtaFail("WiFi failed", "Try again");
  }
  return true;
}

bool pullOtaClearPendingLittleFs() {
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  bool ok = preferences.clear();
  preferences.end();
  return ok;
}

bool pullOtaStorePendingLittleFs(
    const PullOtaManifest &manifest,
    const PullOtaManifest &rollbackManifest) {
  if (!manifest.littlefs.present || !manifest.littlefs.required ||
      !rollbackManifest.littlefs.present || !rollbackManifest.littlefs.required) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  preferences.clear();
  bool ok = preferences.putString("url", manifest.littlefs.url) > 0 &&
            preferences.putUInt("size", (uint32_t)manifest.littlefs.size) > 0 &&
            preferences.putString("sha", manifest.littlefs.sha256) > 0 &&
            preferences.putString("version", manifest.version) > 0 &&
            preferences.putString("rb_url", rollbackManifest.littlefs.url) > 0 &&
            preferences.putUInt("rb_size", (uint32_t)rollbackManifest.littlefs.size) > 0 &&
            preferences.putString("rb_sha", rollbackManifest.littlefs.sha256) > 0 &&
            preferences.putString("rb_ver", rollbackManifest.version) > 0 &&
            preferences.putString("label", manifest.fsPartitionLabel) > 0 &&
            preferences.putUInt("fs_size", manifest.fsPartitionSize) > 0 &&
            preferences.putUInt("fs_schema", manifest.fsSchema) > 0 &&
            preferences.putBool("restore", false) > 0 &&
            preferences.putBool("restore_try", false) > 0 &&
            preferences.putBool("fs_dirty", false) > 0 &&
            preferences.putUChar("target_try", 0) > 0 &&
            preferences.putBool("pending", true) > 0;
  preferences.end();
  return ok;
}

bool pullOtaActivateRollbackLittleFs(const PullOtaPendingLittleFs &pending);
[[noreturn]] void pullOtaRecoveryError();

bool pullOtaLoadPendingLittleFs(PullOtaPendingLittleFs &pending) {
  Preferences preferences;
  if (!preferences.begin("ota_fs", true)) {
    return false;
  }
  bool hasPending = preferences.getBool("pending", false);
  if (!hasPending) {
    preferences.end();
    return false;
  }
  PullOtaPendingLittleFs loaded;
  loaded.present = true;
  loaded.asset.present = true;
  loaded.asset.required = true;
  loaded.asset.url = preferences.getString("url", "");
  loaded.asset.size = (size_t)preferences.getUInt("size", 0);
  loaded.asset.sha256 = preferences.getString("sha", "");
  loaded.asset.sha256.trim();
  loaded.asset.sha256.toLowerCase();
  loaded.version = preferences.getString("version", "");
  loaded.version.trim();
  loaded.restore = preferences.getBool("restore", false);
  loaded.restoreAttempted = preferences.getBool("restore_try", false);
  loaded.filesystemDirty = preferences.getBool("fs_dirty", false);
  loaded.targetAttempts = preferences.getUChar("target_try", 0);
  loaded.rollbackAsset.present = true;
  loaded.rollbackAsset.required = true;
  loaded.rollbackAsset.url = preferences.getString("rb_url", "");
  loaded.rollbackAsset.size = (size_t)preferences.getUInt("rb_size", 0);
  loaded.rollbackAsset.sha256 = preferences.getString("rb_sha", "");
  loaded.rollbackAsset.sha256.trim();
  loaded.rollbackAsset.sha256.toLowerCase();
  loaded.rollbackVersion = preferences.getString("rb_ver", "");
  loaded.rollbackVersion.trim();
  loaded.fsPartitionLabel = preferences.getString("label", "");
  loaded.fsPartitionSize = preferences.getUInt("fs_size", 0);
  loaded.fsSchema = preferences.getUInt("fs_schema", 0);
  preferences.end();
  if (!pullOtaUrlAllowed(loaded.asset.url) ||
      !pullOtaShaLooksValid(loaded.asset.sha256) ||
      loaded.asset.size != HDS_OTA_FS_PARTITION_SIZE ||
      loaded.fsPartitionLabel != HDS_OTA_FS_PARTITION_LABEL ||
      loaded.fsPartitionSize != HDS_OTA_FS_PARTITION_SIZE ||
      loaded.fsSchema != HDS_OTA_FS_SCHEMA) {
    pullOtaClearPendingLittleFs();
    return false;
  }
  if (!loaded.restore &&
      (!pullOtaUrlAllowed(loaded.rollbackAsset.url) ||
       !pullOtaShaLooksValid(loaded.rollbackAsset.sha256) ||
       loaded.rollbackAsset.size != HDS_OTA_FS_PARTITION_SIZE ||
       !pullOtaVersionLooksStable(loaded.rollbackVersion))) {
    pullOtaClearPendingLittleFs();
    return false;
  }
  if (loaded.version != pullOtaCurrentVersion()) {
    if (!loaded.restore && loaded.filesystemDirty &&
        loaded.rollbackVersion == pullOtaCurrentVersion()) {
      if (!pullOtaActivateRollbackLittleFs(loaded)) {
        pullOtaRecoveryError();
      }
      return pullOtaLoadPendingLittleFs(pending);
    }
    pullOtaClearPendingLittleFs();
    return false;
  }
  pending = loaded;
  return true;
}

bool pullOtaActivateRollbackLittleFs(const PullOtaPendingLittleFs &pending) {
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  preferences.clear();
  bool ok = preferences.putString("url", pending.rollbackAsset.url) > 0 &&
            preferences.putUInt("size", (uint32_t)pending.rollbackAsset.size) > 0 &&
            preferences.putString("sha", pending.rollbackAsset.sha256) > 0 &&
            preferences.putString("version", pending.rollbackVersion) > 0 &&
            preferences.putString("label", pending.fsPartitionLabel) > 0 &&
            preferences.putUInt("fs_size", pending.fsPartitionSize) > 0 &&
            preferences.putUInt("fs_schema", pending.fsSchema) > 0 &&
            preferences.putBool("restore", true) > 0 &&
            preferences.putBool("restore_try", false) > 0 &&
            preferences.putBool("pending", true) > 0;
  preferences.end();
  return ok;
}

bool pullOtaBeginRollbackLittleFsAttempt() {
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  bool ok = preferences.putBool("restore_try", true) > 0;
  preferences.end();
  return ok;
}

bool pullOtaBeginTargetLittleFsAttempt(uint8_t attempt) {
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  bool ok = preferences.putUChar("target_try", attempt) > 0;
  preferences.end();
  return ok;
}

bool pullOtaMarkPendingLittleFsDirty() {
  Preferences preferences;
  if (!preferences.begin("ota_fs", false)) {
    return false;
  }
  bool ok = preferences.putBool("fs_dirty", true) > 0;
  preferences.end();
  return ok;
}

bool pullOtaHasPendingLittleFs() {
  PullOtaPendingLittleFs pending;
  return pullOtaLoadPendingLittleFs(pending);
}

bool pullOtaWaitForRelease(unsigned long timeoutMs) {
  unsigned long startedAt = millis();
  while (millis() - startedAt < timeoutMs) {
    if (digitalRead(BUTTON_SQUARE) == HIGH && digitalRead(BUTTON_CIRCLE) == HIGH) {
      return true;
    }
    delay(20);
  }
  return false;
}

void pullOtaDrawReleaseChoice(const PullOtaReleaseList &list, uint8_t index) {
  char page[24];
  snprintf(page, sizeof(page), "%u/%u O next Sq ok", index + 1, list.count);
  pullOtaDraw("Install version", list.releases[index].version.c_str(), page);
}

bool pullOtaPickRelease(const PullOtaReleaseList &list, PullOtaManifest &manifest) {
  if (list.count == 0) {
    return false;
  }
  pullOtaWaitForRelease(1000);
  uint8_t index = 0;
  bool circleWasDown = false;
  bool squareWasDown = false;
  unsigned long circleDownAt = 0;
  unsigned long startedAt = millis();
  pullOtaDrawReleaseChoice(list, index);
  while (millis() - startedAt < HDS_OTA_PICK_TIMEOUT_MS) {
    bool circleDown = digitalRead(BUTTON_CIRCLE) == LOW;
    bool squareDown = digitalRead(BUTTON_SQUARE) == LOW;
    if (circleDown && !circleWasDown) {
      circleDownAt = millis();
    }
    if (circleDown && circleDownAt > 0 &&
        millis() - circleDownAt >= HDS_OTA_CONFIRM_HOLD_MS) {
      return pullOtaFail("Update cancelled");
    }
    if (circleWasDown && !circleDown) {
      index = (index + 1) % list.count;
      pullOtaDrawReleaseChoice(list, index);
      startedAt = millis();
      circleDownAt = 0;
    }
    if (squareDown && !squareWasDown) {
      manifest = list.releases[index];
      pullOtaWaitForRelease(1000);
      return true;
    }
    circleWasDown = circleDown;
    squareWasDown = squareDown;
    delay(20);
  }
  return pullOtaFail("Update cancelled");
}

bool pullOtaConfirmInstall(const PullOtaManifest &manifest) {
  pullOtaWaitForRelease(3000);
  char versionLine[32];
  snprintf(versionLine, sizeof(versionLine), "%s -> %s",
           pullOtaCurrentVersion().c_str(), manifest.version.c_str());
  pullOtaDraw("Install version", versionLine, "Hold square");
  unsigned long startedAt = millis();
  unsigned long heldSince = 0;
  while (millis() - startedAt < HDS_OTA_CONFIRM_TIMEOUT_MS) {
    if (digitalRead(BUTTON_SQUARE) == LOW) {
      if (heldSince == 0) {
        heldSince = millis();
      }
      if (millis() - heldSince >= HDS_OTA_CONFIRM_HOLD_MS) {
        return true;
      }
    } else {
      heldSince = 0;
    }
    delay(20);
  }
  return pullOtaFail("Update cancelled");
}

void pullOtaPauseFilesystemServices() {
  stopWebServer();
  LittleFS.end();
  delay(200);
}

void pullOtaResumeFilesystemServices() {
  LittleFS.begin();
  if (b_wifiEnabled) {
    startWebServer();
  }
}

bool pullOtaStreamAsset(
    const PullOtaAsset &asset,
    int command,
    const char *label,
    bool *filesystemWriteStarted = nullptr) {
  HTTPClient http;
  WiFiClientSecure client;
  if (!pullOtaBeginHttp(http, client, asset.url)) {
    return pullOtaFail("Bad OTA URL", label);
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return pullOtaFail("Download failed", label);
  }
  int contentLength = http.getSize();
  if (contentLength > 0 && (size_t)contentLength != asset.size) {
    http.end();
    return pullOtaFail("Size mismatch", label);
  }
  bool filesystemWrite = command == U_SPIFFS;
  if (filesystemWrite && filesystemWriteStarted != nullptr) {
    if (!pullOtaMarkPendingLittleFsDirty()) {
      http.end();
      return pullOtaFail("FS state failed", label);
    }
    *filesystemWriteStarted = true;
  }
  if (filesystemWrite) {
    pullOtaPauseFilesystemServices();
  }
  if (!Update.begin(asset.size, command)) {
    http.end();
    if (filesystemWrite) {
      pullOtaResumeFilesystemServices();
    }
    return pullOtaFail("Update begin failed", label);
  }
  PullOtaHash hash;
  if (!pullOtaHashBegin(hash)) {
    Update.abort();
    http.end();
    if (filesystemWrite) {
      pullOtaResumeFilesystemServices();
    }
    return pullOtaFail("Hash init failed", label);
  }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[HDS_OTA_BUFFER_BYTES];
  size_t written = 0;
  int lastPercent = -1;
  unsigned long lastRead = millis();
  bool ok = true;
  while ((http.connected() || stream->available()) && written < asset.size) {
    int available = stream->available();
    if (available > 0) {
      size_t request = (size_t)available;
      if (request > sizeof(buffer)) request = sizeof(buffer);
      size_t remaining = asset.size - written;
      if (request > remaining) request = remaining;
      size_t count = stream->readBytes(buffer, request);
      if (count == 0 || !pullOtaHashUpdate(hash, buffer, count) ||
          Update.write(buffer, count) != count) {
        ok = false;
        break;
      }
      written += count;
      uint8_t percent = asset.size == 0 ? 0 : (uint8_t)((written * 100U) / asset.size);
      if ((int)percent != lastPercent && (percent % 5 == 0 || percent == 100)) {
        lastPercent = percent;
        pullOtaDrawProgress(label, percent);
      }
      lastRead = millis();
    } else {
      if (millis() - lastRead > HDS_OTA_HTTP_TIMEOUT_MS) {
        ok = false;
        break;
      }
      delay(1);
    }
  }
  uint8_t digest[32];
  bool hashFinished = pullOtaHashFinish(hash, digest);
  ok = ok && written == asset.size && hashFinished &&
       sha256Matches(digest, asset.sha256);
  if (!ok) {
    Update.abort();
    http.end();
    if (filesystemWrite) {
      pullOtaResumeFilesystemServices();
    }
    return pullOtaFail("Hash mismatch", label);
  }
  if (!Update.end(true)) {
    http.end();
    if (filesystemWrite) {
      pullOtaResumeFilesystemServices();
    }
    return pullOtaFail("Update end failed", label);
  }
  http.end();
  return true;
}

bool pullOtaInstall(
    const PullOtaManifest &manifest,
    const PullOtaManifest &rollbackManifest) {
  b_ota = true;
  if (!pullOtaStorePendingLittleFs(manifest, rollbackManifest)) {
    b_ota = false;
    return pullOtaFail("FS state failed");
  }
  if (!pullOtaStreamAsset(manifest.firmware, U_FLASH, "Firmware")) {
    pullOtaClearPendingLittleFs();
    b_ota = false;
    return false;
  }
  pullOtaDraw("Firmware done", "Restarting", "Web UI next");
  delay(1500);
  ESP.restart();
  return true;
}

bool pullOtaVerifyPendingLittleFs(const PullOtaPendingLittleFs &pending) {
  if (!pullOtaLittleFsPartitionSizeMatches(pending.fsPartitionSize)) {
    return false;
  }
  if (!pullOtaPartitionShaMatches(pending.asset)) {
    return false;
  }
  LittleFS.end();
  if (!LittleFS.begin()) {
    return false;
  }
  return LittleFS.totalBytes() > 0;
}

[[noreturn]] void pullOtaRecoveryError() {
  b_ota = true;
  Serial.println("[pull-ota] UPDATE ERROR - Use HDS updater");
  while (true) {
    pullOtaDraw("UPDATE ERROR", "Use HDS updater!");
    delay(1000);
  }
}

bool pullOtaAttemptPendingLittleFs(
    const PullOtaPendingLittleFs &pending,
    bool &filesystemWriteStarted) {
  pullOtaDraw("Updating web UI", pending.version.c_str());
  if (!pullOtaEnsureWifi(HDS_OTA_PENDING_WIFI_TIMEOUT_MS)) {
    return false;
  }
  if (!pullOtaClockReady()) {
    pullOtaFail("Clock failed", "TLS blocked");
    return false;
  }
  b_ota = true;
  if (!pullOtaStreamAsset(
          pending.asset, U_SPIFFS, "LittleFS", &filesystemWriteStarted)) {
    return false;
  }
  if (!pullOtaVerifyPendingLittleFs(pending)) {
    pullOtaFail("FS verify failed", "Retry later");
    return false;
  }
  return true;
}

bool pullOtaResumePendingLittleFs() {
  PullOtaPendingLittleFs pending;
  if (!pullOtaLoadPendingLittleFs(pending)) {
    return true;
  }
  uint8_t maxAttempts = pending.restore ? 1 : 2;
  uint8_t attempts = pending.restore ? 0 : pending.targetAttempts;
  bool filesystemWriteStarted = pending.filesystemDirty;
  bool updated = false;
  while (attempts < maxAttempts) {
    attempts++;
    bool attemptRecorded = pending.restore
        ? !pending.restoreAttempted && pullOtaBeginRollbackLittleFsAttempt()
        : pullOtaBeginTargetLittleFsAttempt(attempts);
    if (!attemptRecorded) {
      pullOtaRecoveryError();
    }
    bool attemptWriteStarted = false;
    if (pullOtaAttemptPendingLittleFs(pending, attemptWriteStarted)) {
      filesystemWriteStarted = filesystemWriteStarted || attemptWriteStarted;
      updated = true;
      break;
    }
    filesystemWriteStarted = filesystemWriteStarted || attemptWriteStarted;
    if (pending.restore) {
      pullOtaRecoveryError();
    }
  }
  if (!updated) {
    if (filesystemWriteStarted && !pullOtaActivateRollbackLittleFs(pending)) {
      pullOtaRecoveryError();
    }
    return false;
  }
  if (!pullOtaClearPendingLittleFs()) {
    pullOtaFail("FS state failed");
    pullOtaRecoveryError();
  }
  hdsOtaRollbackMarkValid();
  pullOtaDraw("Update done", "Restarting");
  delay(1500);
  ESP.restart();
  return true;
}

void pullOtaRunUpdate() {
  if (pullOtaHasPendingLittleFs()) {
    pullOtaResumePendingLittleFs();
    return;
  }
  pullOtaDraw("Checking update");
  if (!pullOtaEnsureWifi()) {
    return;
  }
  if (!pullOtaClockReady()) {
    pullOtaFail("Clock failed", "TLS blocked");
    return;
  }
  String body;
  if (!pullOtaFetchSignedManifest(body)) {
    pullOtaFail("Signature failed");
    return;
  }
  PullOtaReleaseList catalog;
  if (!pullOtaParseManifest(body, catalog)) {
    pullOtaFail("Manifest invalid");
    return;
  }
  PullOtaManifest rollbackManifest;
  if (!pullOtaFindCurrentRelease(catalog, rollbackManifest)) {
    pullOtaFail("Rollback missing");
    return;
  }
  PullOtaReleaseList releases;
  pullOtaBuildSelectableReleases(catalog, releases);
  if (releases.count == 0) {
    pullOtaDraw("Newest stable", pullOtaCurrentVersion().c_str());
    delay(2000);
    b_ota = false;
    return;
  }
  if (!pullOtaHasNewerRelease(releases)) {
    pullOtaDraw("Newest stable", pullOtaCurrentVersion().c_str());
    delay(1500);
  }
  PullOtaManifest manifest;
  if (!pullOtaPickRelease(releases, manifest)) {
    return;
  }
  if (!pullOtaConfirmInstall(manifest)) {
    return;
  }
  pullOtaInstall(manifest, rollbackManifest);
}

void pullOtaUpdateTask(void *args) {
  (void)args;
  pullOtaRunUpdate();
  b_pullOtaRunning = false;
  vTaskDelete(NULL);
}

void pullOtaUpdate() {
  if (b_pullOtaRunning) {
    return;
  }
  b_pullOtaRunning = true;
  b_ota = true;
  BaseType_t started = xTaskCreate(
      pullOtaUpdateTask,
      "Pull OTA",
      HDS_OTA_TASK_STACK_BYTES,
      NULL,
      1,
      NULL);
  if (started != pdPASS) {
    b_pullOtaRunning = false;
    pullOtaFail("OTA task failed");
  }
}

#endif

#endif
