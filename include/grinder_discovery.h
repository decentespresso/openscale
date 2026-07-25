#ifndef GRINDER_DISCOVERY_H
#define GRINDER_DISCOVERY_H

#include <esp_err.h>
#include <mdns.h>
#include <string.h>
#include <strings.h>
#include "wifi_setup.h"

#ifndef GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS
#define GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS 250
#endif

#ifndef GRINDER_MDNS_MAX_CANDIDATES
#define GRINDER_MDNS_MAX_CANDIDATES 8
#endif

#ifndef GRINDER_MDNS_FOLLOWUP_TIMEOUT_MS
#define GRINDER_MDNS_FOLLOWUP_TIMEOUT_MS 1000
#endif

struct GrinderMdnsCandidate {
  bool used = false;
  char instance[64] = { 0 };
  char hostname[64] = { 0 };
  char mac[18] = { 0 };
  char proto[8] = { 0 };
  char model[32] = { 0 };
  uint16_t port = 0;
  uint32_t ipv4 = 0;
};

static inline void grinderSaveSelectedDiscovery(const GrinderDiscoveredPlug &plug) {
  grinderCopyCString(grinderSettings.selectedMac, sizeof(grinderSettings.selectedMac), plug.mac);
  grinderCopyCString(grinderSettings.hostname, sizeof(grinderSettings.hostname), plug.hostname);
  grinderSettings.lastIp = plug.ip;
  grinderSaveSettings();
}

static inline void grinderClearDiscoveries() {
  grinderRuntime.discoveredCount = 0;
  for (uint8_t i = 0; i < 8; i++) {
    grinderRuntime.discovered[i] = GrinderDiscoveredPlug();
  }
}

static inline bool grinderDiscoveryMacExists(const char *mac) {
  for (uint8_t i = 0; i < grinderRuntime.discoveredCount; i++) {
    if (strcmp(grinderRuntime.discovered[i].mac, mac) == 0) {
      return true;
    }
  }
  return false;
}

static inline bool grinderAddDiscovery(const char *mac, const char *hostname, IPAddress ip) {
  if (grinderRuntime.discoveredCount >= 8 || !grinderIsMac(mac) || grinderDiscoveryMacExists(mac)) {
    return false;
  }

  GrinderDiscoveredPlug plug;
  grinderCopyCString(plug.mac, sizeof(plug.mac), mac);
  grinderCopyCString(plug.hostname, sizeof(plug.hostname), hostname);
  plug.ip = ip;
  grinderRuntime.discovered[grinderRuntime.discoveredCount++] = plug;
  return true;
}

static inline const char *grinderMdnsTxt(const mdns_result_t *result, const char *key) {
  if (result == nullptr || key == nullptr) {
    return "";
  }

  for (size_t i = 0; i < result->txt_count; i++) {
    if (result->txt[i].key != nullptr && strcmp(result->txt[i].key, key) == 0) {
      return result->txt[i].value != nullptr ? result->txt[i].value : "";
    }
  }
  return "";
}

static inline uint32_t grinderMdnsIpv4(const mdns_result_t *result) {
  if (result == nullptr) {
    return 0;
  }

  for (mdns_ip_addr_t *address = result->addr; address != nullptr; address = address->next) {
    if (address->addr.type == ESP_IPADDR_TYPE_V4) {
      return address->addr.u_addr.ip4.addr;
    }
  }
  return 0;
}

static inline void grinderStripLocalSuffix(char *host) {
  if (host == nullptr) {
    return;
  }

  const size_t length = strlen(host);
  const char suffix[] = ".local";
  const size_t suffixLength = sizeof(suffix) - 1;
  if (length > suffixLength && strcasecmp(host + length - suffixLength, suffix) == 0) {
    host[length - suffixLength] = 0;
  }
}

static inline void grinderMdnsCopyIfPresent(char *output, size_t outputSize, const char *input) {
  if (input != nullptr && input[0] != 0) {
    grinderCopyCString(output, outputSize, input);
  }
}

static inline void grinderMdnsMergeCandidate(GrinderMdnsCandidate &target,
                                              const GrinderMdnsCandidate &update) {
  target.used = target.used || update.used;
  grinderMdnsCopyIfPresent(target.instance, sizeof(target.instance), update.instance);
  grinderMdnsCopyIfPresent(target.hostname, sizeof(target.hostname), update.hostname);
  grinderMdnsCopyIfPresent(target.mac, sizeof(target.mac), update.mac);
  grinderMdnsCopyIfPresent(target.proto, sizeof(target.proto), update.proto);
  grinderMdnsCopyIfPresent(target.model, sizeof(target.model), update.model);

  if (update.port != 0) {
    target.port = update.port;
  }
  if (update.ipv4 != 0) {
    target.ipv4 = update.ipv4;
  }
}

static inline GrinderMdnsCandidate grinderMdnsCandidateFromResult(const mdns_result_t *result) {
  GrinderMdnsCandidate candidate;
  if (result == nullptr) {
    return candidate;
  }

  candidate.used = true;
  grinderMdnsCopyIfPresent(candidate.instance, sizeof(candidate.instance), result->instance_name);
  grinderMdnsCopyIfPresent(candidate.hostname, sizeof(candidate.hostname), result->hostname);
  grinderMdnsCopyIfPresent(candidate.mac, sizeof(candidate.mac), grinderMdnsTxt(result, "mac"));
  grinderMdnsCopyIfPresent(candidate.proto, sizeof(candidate.proto), grinderMdnsTxt(result, "proto"));
  grinderMdnsCopyIfPresent(candidate.model, sizeof(candidate.model), grinderMdnsTxt(result, "model"));
  candidate.port = result->port;
  candidate.ipv4 = grinderMdnsIpv4(result);
  return candidate;
}

static inline int grinderMdnsFindCandidate(const GrinderMdnsCandidate *candidates,
                                            const char *instance) {
  if (instance == nullptr || instance[0] == 0) {
    return -1;
  }

  for (uint8_t i = 0; i < GRINDER_MDNS_MAX_CANDIDATES; i++) {
    if (candidates[i].used && strcasecmp(candidates[i].instance, instance) == 0) {
      return i;
    }
  }
  return -1;
}

static inline int grinderMdnsFindFreeCandidate(const GrinderMdnsCandidate *candidates) {
  for (uint8_t i = 0; i < GRINDER_MDNS_MAX_CANDIDATES; i++) {
    if (!candidates[i].used) {
      return i;
    }
  }
  return -1;
}

static void grinderMdnsBrowseNotify(mdns_result_t *result) {
  if (result == nullptr || result->instance_name == nullptr || result->instance_name[0] == 0) {
    return;
  }

  const GrinderMdnsCandidate update = grinderMdnsCandidateFromResult(result);

  portENTER_CRITICAL(&grinderMdnsMux);
  GrinderMdnsCandidate *candidates = grinderMdnsCandidateBuffer;
  if (candidates == nullptr) {
    portEXIT_CRITICAL(&grinderMdnsMux);
    return;
  }

  int index = grinderMdnsFindCandidate(candidates, update.instance);
  if (result->ttl == 0) {
    if (index >= 0) {
      candidates[index] = GrinderMdnsCandidate();
    }
    portEXIT_CRITICAL(&grinderMdnsMux);
    return;
  }

  if (index < 0) {
    index = grinderMdnsFindFreeCandidate(candidates);
  }
  if (index >= 0) {
    grinderMdnsMergeCandidate(candidates[index], update);
  }
  portEXIT_CRITICAL(&grinderMdnsMux);
}

static inline void grinderMdnsStartCollection(GrinderMdnsCandidate *candidates) {
  for (uint8_t i = 0; i < GRINDER_MDNS_MAX_CANDIDATES; i++) {
    candidates[i] = GrinderMdnsCandidate();
  }

  portENTER_CRITICAL(&grinderMdnsMux);
  grinderMdnsCandidateBuffer = candidates;
  portEXIT_CRITICAL(&grinderMdnsMux);
}

static inline void grinderMdnsStopCollection() {
  portENTER_CRITICAL(&grinderMdnsMux);
  grinderMdnsCandidateBuffer = nullptr;
  portEXIT_CRITICAL(&grinderMdnsMux);
}

static inline bool grinderMdnsBrowseRound(uint32_t windowMs, bool debug) {
  if (mdns_browse_new("_grinderplug", "_tcp", grinderMdnsBrowseNotify) == nullptr) {
    if (debug) {
      Serial.println("[grinder] mdns browse start failed");
    }
    return false;
  }

  delay(windowMs);
  const esp_err_t deleteErr = mdns_browse_delete("_grinderplug", "_tcp");
  if (debug) {
    Serial.printf("[grinder] mdns browse window=%lu delete=%s\n",
                  (unsigned long)windowMs,
                  esp_err_to_name(deleteErr));
  }
  delay(50);
  return deleteErr == ESP_OK;
}

static inline void grinderMdnsMergeResult(GrinderMdnsCandidate &candidate,
                                           const mdns_result_t *result) {
  const GrinderMdnsCandidate update = grinderMdnsCandidateFromResult(result);
  grinderMdnsMergeCandidate(candidate, update);
}

static inline void grinderMdnsResolveSrv(GrinderMdnsCandidate &candidate, bool debug) {
  if (candidate.instance[0] == 0 || (candidate.hostname[0] != 0 && candidate.port != 0)) {
    return;
  }

  mdns_result_t *results = nullptr;
  const esp_err_t err = mdns_query_srv(candidate.instance,
                                       "_grinderplug",
                                       "_tcp",
                                       GRINDER_MDNS_FOLLOWUP_TIMEOUT_MS,
                                       &results);
  for (mdns_result_t *result = results; result != nullptr; result = result->next) {
    grinderMdnsMergeResult(candidate, result);
  }
  if (results != nullptr) {
    mdns_query_results_free(results);
  }

  if (debug) {
    Serial.printf("[grinder] mdns srv instance=%s err=%s host=%s port=%u\n",
                  candidate.instance,
                  esp_err_to_name(err),
                  candidate.hostname,
                  candidate.port);
  }
}

static inline void grinderMdnsResolveTxt(GrinderMdnsCandidate &candidate, bool debug) {
  if (candidate.instance[0] == 0 ||
      (grinderIsMac(candidate.mac) && strcmp(candidate.proto, "1") == 0)) {
    return;
  }

  mdns_result_t *results = nullptr;
  const esp_err_t err = mdns_query_txt(candidate.instance,
                                       "_grinderplug",
                                       "_tcp",
                                       GRINDER_MDNS_FOLLOWUP_TIMEOUT_MS,
                                       &results);
  for (mdns_result_t *result = results; result != nullptr; result = result->next) {
    grinderMdnsMergeResult(candidate, result);
  }
  if (results != nullptr) {
    mdns_query_results_free(results);
  }

  if (debug) {
    Serial.printf("[grinder] mdns txt instance=%s err=%s mac=%s proto=%s model=%s\n",
                  candidate.instance,
                  esp_err_to_name(err),
                  candidate.mac,
                  candidate.proto,
                  candidate.model);
  }
}

static inline void grinderMdnsResolveA(GrinderMdnsCandidate &candidate, bool debug) {
  if (candidate.ipv4 != 0 || candidate.hostname[0] == 0) {
    return;
  }

  char host[sizeof(candidate.hostname)] = { 0 };
  grinderCopyCString(host, sizeof(host), candidate.hostname);
  grinderStripLocalSuffix(host);

  esp_ip4_addr_t address;
  address.addr = 0;
  const esp_err_t err = mdns_query_a(host, GRINDER_MDNS_FOLLOWUP_TIMEOUT_MS, &address);
  if (err == ESP_OK && address.addr != 0) {
    candidate.ipv4 = address.addr;
  }

  if (debug) {
    const IPAddress ip(candidate.ipv4);
    Serial.printf("[grinder] mdns A host=%s err=%s ip=%s\n",
                  host,
                  esp_err_to_name(err),
                  ip.toString().c_str());
  }
}

static inline bool grinderAddResolvedMdnsCandidate(GrinderMdnsCandidate &candidate,
                                                    bool debug) {
  if (!candidate.used || candidate.instance[0] == 0) {
    return false;
  }

  grinderMdnsResolveSrv(candidate, debug);
  grinderMdnsResolveTxt(candidate, debug);
  grinderMdnsResolveA(candidate, debug);

  const IPAddress ip(candidate.ipv4);
  const bool valid = candidate.port == GRINDER_TCP_PORT &&
                     grinderIsMac(candidate.mac) &&
                     strcmp(candidate.proto, "1") == 0 &&
                     grinderIpValid(ip);

  if (debug) {
    Serial.printf("[grinder] mdns candidate instance=%s host=%s port=%u mac=%s "
                  "proto=%s model=%s ip=%s valid=%d\n",
                  candidate.instance,
                  candidate.hostname,
                  candidate.port,
                  candidate.mac,
                  candidate.proto,
                  candidate.model,
                  ip.toString().c_str(),
                  valid ? 1 : 0);
  }

  return valid && grinderAddDiscovery(candidate.mac, candidate.hostname, ip);
}

static inline uint8_t grinderDiscoverPlugsByMdnsBrowse(bool debug) {
  GrinderMdnsCandidate candidates[GRINDER_MDNS_MAX_CANDIDATES];

#ifdef ESP_MDNS_VERSION_NUMBER
  if (debug) {
    Serial.printf("[grinder] esp-mdns=%s\n", ESP_MDNS_VERSION_NUMBER);
  }
#endif

  for (uint8_t attempt = 0;
       attempt < 2 && grinderRuntime.discoveredCount == 0;
       attempt++) {
    grinderMdnsStartCollection(candidates);
    grinderMdnsBrowseRound(2500, debug);
    grinderMdnsStopCollection();
    for (uint8_t i = 0;
         i < GRINDER_MDNS_MAX_CANDIDATES && grinderRuntime.discoveredCount < 8;
         i++) {
      grinderAddResolvedMdnsCandidate(candidates[i], debug);
    }
  }

  if (debug) {
    Serial.printf("[grinder] mdns discovered=%u\n", grinderRuntime.discoveredCount);
  }
  return grinderRuntime.discoveredCount;
}

static inline uint8_t grinderDiscoverPlugs(bool debugMdns = true) {
  grinderClearDiscoveries();

  if (!b_wifiEnabled || WiFi.status() != WL_CONNECTED) {
    grinderSetStatus("wifi wait");
    return 0;
  }
  if (!wifiEnsureMdnsReadyForSta()) {
    grinderSetStatus("mdns wait");
    return 0;
  }

  grinderSetStatus("finding");
  grinderDiscoverPlugsByMdnsBrowse(debugMdns);
  grinderSetStatus(grinderRuntime.discoveredCount == 0 ? "none found" : "found");
  return grinderRuntime.discoveredCount;
}

#endif
