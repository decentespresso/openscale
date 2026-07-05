#ifndef GRINDER_DISCOVERY_H
#define GRINDER_DISCOVERY_H

#include <esp_err.h>
#include <esp_wifi.h>
#include <mdns.h>
#include "wifi_setup.h"

#ifndef GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS
#define GRINDER_DISCOVERY_CONNECT_TIMEOUT_MS 90
#endif

#ifndef GRINDER_DISCOVERY_READ_TIMEOUT_MS
#define GRINDER_DISCOVERY_READ_TIMEOUT_MS 180
#endif

#ifndef GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK
#define GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK 0
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

static inline bool grinderAddDiscovery(const char *mac, const char *name, const char *hostname, IPAddress ip) {
  if (grinderRuntime.discoveredCount >= 8 || !grinderIsMac(mac) || grinderDiscoveryMacExists(mac)) {
    return false;
  }
  GrinderDiscoveredPlug plug;
  plug.valid = true;
  grinderCopyCString(plug.mac, sizeof(plug.mac), mac);
  grinderCopyCString(plug.name, sizeof(plug.name), name);
  grinderCopyCString(plug.hostname, sizeof(plug.hostname), hostname);
  plug.ip = ip;
  plug.port = GRINDER_TCP_PORT;
  grinderRuntime.discovered[grinderRuntime.discoveredCount++] = plug;
  return true;
}

static inline bool grinderAddDiscoveryFromMdnsIndex(int index) {
  const String mac = MDNS.txt(index, "mac");
  const String model = MDNS.txt(index, "model");
  const String proto = MDNS.txt(index, "proto");
  const String name = MDNS.txt(index, "name");
  const String instance = MDNS.instanceName(index);
  const String hostname = MDNS.hostname(index);
  const String address = MDNS.address(index).toString();
  Serial.printf("[grinder] mdns[%d] port=%u mac=%s model=%s proto=%s host=%s ip=%s txt=%d\n",
                index,
                MDNS.port(index),
                mac.c_str(),
                model.c_str(),
                proto.c_str(),
                hostname.c_str(),
                address.c_str(),
                MDNS.numTxt(index));
  if (MDNS.port(index) != GRINDER_TCP_PORT) {
    Serial.println("[grinder] mdns reject port");
    return false;
  }
  if (model != "NOUS_A6T" || proto != "1" || !grinderIsMac(mac.c_str())) {
    Serial.println("[grinder] mdns reject txt");
    return false;
  }
  char nameOutput[32] = { 0 };
  grinderCopyString(nameOutput, sizeof(nameOutput), name);
  if (nameOutput[0] == 0) {
    grinderCopyString(nameOutput, sizeof(nameOutput), instance);
  }
  char hostnameOutput[64] = { 0 };
  grinderCopyString(hostnameOutput, sizeof(hostnameOutput), hostname);
  const bool added = grinderAddDiscovery(mac.c_str(), nameOutput, hostnameOutput, MDNS.address(index));
  Serial.printf("[grinder] mdns add %s\n", added ? "ok" : "skip");
  return added;
}

static inline void grinderPrepareWifiForDiscovery() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
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

static inline bool grinderAddDiscoveryFromRawMdnsResult(const mdns_result_t *result) {
  if (result == nullptr || result->port != GRINDER_TCP_PORT) {
    return false;
  }
  const char *mac = grinderRawMdnsTxt(result, "mac");
  const char *model = grinderRawMdnsTxt(result, "model");
  const char *proto = grinderRawMdnsTxt(result, "proto");
  if (strcmp(model, "NOUS_A6T") != 0 || strcmp(proto, "1") != 0 || !grinderIsMac(mac)) {
    return false;
  }
  const char *name = grinderRawMdnsTxt(result, "name");
  if (name[0] == 0) {
    name = result->instance_name != nullptr ? result->instance_name : "";
  }
  const char *hostname = result->hostname != nullptr ? result->hostname : "";
  const IPAddress ip = grinderRawMdnsIpv4(result);
  return grinderIpValid(ip) && grinderAddDiscovery(mac, name, hostname, ip);
}

static inline uint8_t grinderDiscoverPlugsByRawMdns(uint32_t timeoutMs, bool debug) {
  mdns_result_t *results = nullptr;
  const esp_err_t err = mdns_query_ptr("_grinderplug", "_tcp", timeoutMs, 8, &results);
  if (debug) {
    Serial.printf("[grinder] raw mdns err=%s results=%p wifi=%d ip=%s\n",
                  esp_err_to_name(err),
                  results,
                  (int)WiFi.status(),
                  WiFi.localIP().toString().c_str());
  }
  uint8_t count = 0;
  for (mdns_result_t *result = results; result != nullptr; result = result->next) {
    count++;
    if (debug) {
      Serial.printf("[grinder] raw mdns[%d] instance=%s host=%s port=%u txt=%u\n",
                    count,
                    result->instance_name ? result->instance_name : "",
                    result->hostname ? result->hostname : "",
                    result->port,
                    (unsigned)result->txt_count);
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

static inline void grinderDiscoverPlugsByMdns() {
  if (!wifiEnsureMdnsReadyForSta()) {
    grinderSetStatus("mdns wait");
    return;
  }
  const int count = MDNS.queryService("grinderplug", "tcp");
  Serial.printf("[grinder] mdns query _grinderplug._tcp count=%d wifi=%d ip=%s\n",
                count,
                (int)WiFi.status(),
                WiFi.localIP().toString().c_str());
  for (int i = 0; i < count && grinderRuntime.discoveredCount < 8; i++) {
    grinderAddDiscoveryFromMdnsIndex(i);
  }
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
      if (byteValue < 32 || byteValue > 126) {
        return false;
      }
      if (length + 1 >= lineSize) {
        return false;
      }
      line[length++] = c;
    }
    delay(1);
  }
  return false;
}

static inline bool grinderProbeDiscoveryIp(IPAddress ip) {
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
  return grinderAddDiscovery(response.plugMac, response.kind == GRINDER_TCP_RESPONSE_BUSY ? "busy" : "tcp", "", ip);
}

static inline void grinderDiscoverPlugsByTcpScan() {
  const IPAddress local = WiFi.localIP();
  if (!grinderIpValid(local)) {
    return;
  }
  const uint8_t ownHost = local[3];
  for (uint16_t radius = 1; radius < 255 && grinderRuntime.discoveredCount < 8; radius++) {
    if ((uint16_t)ownHost + radius <= 254) {
      IPAddress high(local[0], local[1], local[2], ownHost + radius);
      grinderProbeDiscoveryIp(high);
    }
    if (radius < ownHost) {
      IPAddress low(local[0], local[1], local[2], ownHost - radius);
      grinderProbeDiscoveryIp(low);
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
  grinderPrepareWifiForDiscovery();
  grinderSetStatus("finding");
  for (uint8_t attempt = 0; attempt < attempts && grinderRuntime.discoveredCount == 0; attempt++) {
    grinderDiscoverPlugsByMdns();
    if (grinderRuntime.discoveredCount == 0 && attempt + 1 < attempts) {
      delay(500);
    }
  }
  if (debugRaw && grinderRuntime.discoveredCount == 0) {
    grinderDebugRawMdnsQuery();
  } else if (!debugRaw && grinderRuntime.discoveredCount == 0) {
    grinderDiscoverPlugsByRawMdns(350, false);
  }
#if GRINDER_DISCOVERY_ENABLE_TCP_FALLBACK
  if (grinderRuntime.discoveredCount == 0) {
    grinderSetStatus("scan tcp");
    grinderDiscoverPlugsByTcpScan();
  }
#endif
  if (grinderRuntime.discoveredCount == 0) {
    grinderSetStatus("none found");
  } else {
    grinderSetStatus("found");
  }
  return grinderRuntime.discoveredCount;
}

static inline bool grinderFindSelectedByMdns(GrinderDiscoveredPlug *plug, bool debugRaw = true) {
  if (plug == nullptr || !grinderSelectedMacSet()) {
    return false;
  }
  grinderDiscoverPlugs(debugRaw, debugRaw ? 3 : 1);
  for (uint8_t i = 0; i < grinderRuntime.discoveredCount; i++) {
    if (strcmp(grinderRuntime.discovered[i].mac, grinderSettings.selectedMac) == 0) {
      *plug = grinderRuntime.discovered[i];
      grinderSaveLookupHintsIfChanged(plug->hostname, plug->ip);
      return true;
    }
  }
  return false;
}

#endif
