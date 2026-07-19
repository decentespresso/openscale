#ifndef GRINDER_DISCOVERY_H
#define GRINDER_DISCOVERY_H

#include <esp_err.h>
#include <mdns.h>
#include <strings.h>
#include "wifi_setup.h"

#ifndef GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS
#define GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS 250
#endif

#ifndef GRINDER_DISCOVERY_READ_TIMEOUT_MS
#define GRINDER_DISCOVERY_READ_TIMEOUT_MS 350
#endif

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

static inline bool grinderDiscoveryIpExists(IPAddress ip) {
  for (uint8_t i = 0; i < grinderRuntime.discoveredCount; i++) {
    if (grinderRuntime.discovered[i].ip == ip) {
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

static inline const char *grinderRawMdnsTxt(const mdns_result_t *result, const char *key) {
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

static inline IPAddress grinderRawMdnsIpv4(const mdns_result_t *result) {
  if (result == nullptr) {
    return IPAddress((uint32_t)0);
  }
  for (mdns_ip_addr_t *address = result->addr; address != nullptr; address = address->next) {
    if (address->addr.type == ESP_IPADDR_TYPE_V4) {
      return IPAddress(address->addr.u_addr.ip4.addr);
    }
  }
  return IPAddress((uint32_t)0);
}

static inline void grinderStripLocalSuffix(char *host) {
  if (host == nullptr) {
    return;
  }
  const size_t len = strlen(host);
  const char suffix[] = ".local";
  const size_t suffixLen = sizeof(suffix) - 1;
  if (len > suffixLen && strcasecmp(host + len - suffixLen, suffix) == 0) {
    host[len - suffixLen] = 0;
  }
}

static inline IPAddress grinderResolveRawMdnsIpv4(const mdns_result_t *result) {
  const IPAddress ip = grinderRawMdnsIpv4(result);
  if (grinderIpValid(ip)) {
    return ip;
  }
  if (result == nullptr || result->hostname == nullptr || result->hostname[0] == 0) {
    return IPAddress((uint32_t)0);
  }
  char host[64] = { 0 };
  grinderCopyCString(host, sizeof(host), result->hostname);
  grinderStripLocalSuffix(host);
  esp_ip4_addr_t address;
  address.addr = 0;
  if (mdns_query_a(host, 2000, &address) == ESP_OK && address.addr != 0) {
    return IPAddress(address.addr);
  }
  return IPAddress((uint32_t)0);
}

static inline bool grinderProbeDiscoveryIp(IPAddress ip, const char *hostname);

static inline bool grinderAddDiscoveryFromRawMdnsResult(const mdns_result_t *result) {
  if (result == nullptr) {
    return false;
  }
  if (result->port != 0 && result->port != GRINDER_TCP_PORT) {
    return false;
  }
  const char *mac = grinderRawMdnsTxt(result, "mac");
  const char *proto = grinderRawMdnsTxt(result, "proto");
  if ((proto[0] != 0 && strcmp(proto, "1") != 0) ||
      (mac[0] != 0 && !grinderIsMac(mac))) {
    return false;
  }
  const char *hostname = result->hostname != nullptr ? result->hostname : "";
  const IPAddress ip = grinderResolveRawMdnsIpv4(result);
  if (!grinderIpValid(ip)) {
    return false;
  }
  if (result->port == GRINDER_TCP_PORT && grinderIsMac(mac)) {
    return grinderAddDiscovery(mac, hostname, ip);
  }
  return grinderProbeDiscoveryIp(ip, hostname);
}

static inline uint8_t grinderDiscoverPlugsByRawMdns(uint32_t timeoutMs, bool debug) {
  mdns_result_t *results = nullptr;
  const esp_err_t err = mdns_query_ptr("_grinderplug", "_tcp", timeoutMs, 8, &results);
  if (debug) {
    Serial.printf("[grinder] raw mdns ptr err=%s results=%p timeout=%lu wifi=%d ip=%s rssi=%d heap=%u\n",
                  esp_err_to_name(err),
                  results,
                  (unsigned long)timeoutMs,
                  (int)WiFi.status(),
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  ESP.getFreeHeap());
  }
  if (err != ESP_OK || results == nullptr) {
    if (results != nullptr) {
      mdns_query_results_free(results);
    }
    return grinderRuntime.discoveredCount;
  }
  uint8_t count = 0;
  for (mdns_result_t *result = results; result != nullptr && grinderRuntime.discoveredCount < 8; result = result->next) {
    count++;
    if (debug) {
      Serial.printf("[grinder] raw mdns result instance=%s host=%s port=%u txt=%u addr=%p\n",
                    result->instance_name ? result->instance_name : "",
                    result->hostname ? result->hostname : "",
                    result->port,
                    (unsigned)result->txt_count,
                    result->addr);
      for (size_t t = 0; t < result->txt_count; t++) {
        Serial.printf("[grinder] raw mdns[%d] txt[%u] %s=%s\n",
                      count,
                      (unsigned)t,
                      result->txt[t].key ? result->txt[t].key : "",
                      result->txt[t].value ? result->txt[t].value : "");
      }
      const IPAddress ip = grinderRawMdnsIpv4(result);
      if (grinderIpValid(ip)) {
        Serial.printf("[grinder] raw mdns[%d] A=%s\n", count, ip.toString().c_str());
      }
    }
    grinderAddDiscoveryFromRawMdnsResult(result);
  }
  if (results != nullptr) {
    mdns_query_results_free(results);
  }
  if (debug) {
    Serial.printf("[grinder] raw mdns count=%d\n", count);
  }
  return grinderRuntime.discoveredCount;
}

static inline void grinderDebugRawMdnsQuery() {
  grinderDiscoverPlugsByRawMdns(5000, true);
}

static inline bool grinderDiscoveryReadLine(WiFiClient &client, char *line, size_t lineSize, uint32_t deadline) {
  if (line == nullptr || lineSize == 0) {
    return false;
  }
  uint16_t length = 0;
  bool pendingCr = false;
  line[0] = 0;
  while ((int32_t)(millis() - deadline) < 0) {
    while (client.available()) {
      const int value = client.read();
      if (value < 0) {
        continue;
      }
      const char c = (char)value;
      if (c == '\r') {
        pendingCr = true;
        continue;
      }
      if (c == '\n') {
        line[length] = 0;
        return length > 0;
      }
      if (pendingCr) {
        if (length + 1 >= lineSize) {
          return false;
        }
        line[length++] = '\r';
        pendingCr = false;
      }
      const uint8_t byteValue = (uint8_t)c;
      if (byteValue < 32 || byteValue > 126 || length + 1 >= lineSize) {
        return false;
      }
      line[length++] = c;
    }
    delay(1);
  }
  return false;
}

static inline bool grinderProbeDiscoveryIp(IPAddress ip, const char *hostname) {
  if (!grinderIpValid(ip) || ip == WiFi.localIP() || grinderDiscoveryIpExists(ip)) {
    return false;
  }
  WiFiClient probe;
  probe.setTimeout(50);
  if (!probe.connect(ip, GRINDER_TCP_PORT, GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS)) {
    probe.stop();
    return false;
  }
  probe.setNoDelay(true);
  char hello[32] = { 0 };
  if (grinderRuntime.scaleMac[0] == 0) {
    grinderFormatScaleMac();
  }
  grinderFormatHello(hello, sizeof(hello), grinderRuntime.scaleMac);
  probe.write((const uint8_t *)hello, strlen(hello));
  probe.write((const uint8_t *)"\n", 1);
  char line[GRINDER_TCP_MAX_LINE_LENGTH + 1] = { 0 };
  const bool lineRead = grinderDiscoveryReadLine(probe, line, sizeof(line), millis() + GRINDER_DISCOVERY_READ_TIMEOUT_MS);
  GrinderTcpResponse response;
  const bool parsed = lineRead && grinderParseResponse(line, &response);
  if (parsed && response.kind == GRINDER_TCP_RESPONSE_OK) {
    probe.write((const uint8_t *)"BYE\n", 4);
  }
  probe.stop();
  if (!parsed || !grinderIsMac(response.plugMac)) {
    return false;
  }
  if (response.kind != GRINDER_TCP_RESPONSE_OK && response.kind != GRINDER_TCP_RESPONSE_BUSY) {
    return false;
  }
  Serial.printf("[grinder] tcp found %s at %s %s\n",
                response.plugMac,
                ip.toString().c_str(),
                response.kind == GRINDER_TCP_RESPONSE_BUSY ? "busy" : "ok");
  return grinderAddDiscovery(response.plugMac, hostname, ip);
}

static inline void grinderDiscoverPlugsByTcpScan() {
  const IPAddress local = WiFi.localIP();
  if (!grinderIpValid(local)) {
    return;
  }
  if (grinderIpValid(grinderSettings.lastIp)) {
    for (uint8_t attempt = 0; attempt < 3 && grinderRuntime.discoveredCount == 0; attempt++) {
      grinderProbeDiscoveryIp(grinderSettings.lastIp, grinderSettings.hostname);
      if (grinderRuntime.discoveredCount == 0) {
        delay(250);
      }
    }
  }
  const uint8_t ownHost = local[3];
  for (uint16_t radius = 1; radius < 255 && grinderRuntime.discoveredCount < 8; radius++) {
    if ((uint16_t)ownHost + radius <= 254) {
      const IPAddress ip(local[0], local[1], local[2], ownHost + radius);
      if (ip != grinderSettings.lastIp) {
        grinderProbeDiscoveryIp(ip, "");
      }
    }
    if (radius < ownHost) {
      const IPAddress ip(local[0], local[1], local[2], ownHost - radius);
      if (ip != grinderSettings.lastIp) {
        grinderProbeDiscoveryIp(ip, "");
      }
    }
    delay(1);
  }
}

static inline uint8_t grinderDiscoverPlugs(bool debugRaw = true, uint8_t attempts = 3) {
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
  const uint32_t timeouts[] = { 1500, 2500, 3500 };
  for (uint8_t attempt = 0; attempt < attempts && grinderRuntime.discoveredCount == 0; attempt++) {
    const uint8_t timeoutIndex = attempt < 3 ? attempt : 2;
    grinderDiscoverPlugsByRawMdns(timeouts[timeoutIndex], debugRaw);
    if (grinderRuntime.discoveredCount == 0 && attempt + 1 < attempts) {
      delay(350);
    }
  }
  if (grinderRuntime.discoveredCount == 0) {
    grinderSetStatus("scan tcp");
    grinderDiscoverPlugsByTcpScan();
  }
  if (debugRaw && grinderRuntime.discoveredCount == 0) {
    grinderDebugRawMdnsQuery();
  }
  if (grinderRuntime.discoveredCount == 0) {
    grinderSetStatus("none found");
  } else {
    grinderSetStatus("found");
  }
  return grinderRuntime.discoveredCount;
}

#endif
