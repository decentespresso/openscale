#ifndef WIFI_SETUP_ROUTES_H
#define WIFI_SETUP_ROUTES_H

#include "wifi_setup.h"

static bool wifiRequestOriginAllowed(AsyncWebServerRequest *request) {
  if (request->hasHeader("Sec-Fetch-Site") &&
      request->getHeader("Sec-Fetch-Site")->value() == "cross-site") return false;
  if (!request->hasHeader("Origin")) return true;
  String host = request->host();
  host.toLowerCase();
  if (host.endsWith(":80")) host.remove(host.length() - 3);
  const WifiSetupStatus status = wifiReadSetupStatus();
  const String name = wifiDeviceName();
  if (host != name && host != name + ".local" && host != status.ip) return false;
  String origin = request->getHeader("Origin")->value();
  origin.toLowerCase();
  if (origin.endsWith(":80")) origin.remove(origin.length() - 3);
  return origin == "http://" + host;
}

static void wifiSendJson(AsyncWebServerRequest *request, int code, JsonDocument &json) {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->setCode(code);
  response->addHeader("Cache-Control", "no-store");
  serializeJson(json, *response);
  request->send(response);
}

static bool parseWifiSetupCredentials(JsonVariant &json, WifiCredentials &credentials,
                                      bool &reset) {
  JsonObject object = json.as<JsonObject>();
  if (object.isNull() || !object["ssid"].is<const char *>() ||
      (!object["pass"].isNull() && !object["pass"].is<const char *>())) return false;
  const JsonString ssid = object["ssid"].as<JsonString>();
  const JsonString pass = object["pass"].as<JsonString>();
  const char *password = pass.c_str() != nullptr ? pass.c_str() : "";
  reset = ssid.size() == 0 && pass.size() == 0;
  return reset || wifiNormalizeCredentials(ssid.c_str(), ssid.size(), password,
                                           pass.size(), credentials);
}

static void wifiSendAccepted(AsyncWebServerRequest *request, uint32_t id,
                             const char *ssid) {
  JsonDocument response;
  response["operation_id"] = id;
  response["state"] = "queued";
  response["ssid"] = ssid;
  response["mdns_name"] = wifiDeviceName();
  response["mdns_available"] = HDS_FEATURE_MDNS != 0;
  response["restarting"] = false;
  wifiSendJson(request, 202, response);
}

void registerWifiSetupRoutes(AsyncWebServer &server) {
  auto *handler = new AsyncCallbackJsonWebHandler(
      "/setup/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!wifiRequestOriginAllowed(request)) {
          request->send(403, "application/json", "{\"error\":\"origin_denied\"}");
          return;
        }
        WifiCredentials credentials;
        bool reset = false;
        if (!parseWifiSetupCredentials(json, credentials, reset)) {
          request->send(400, "application/json", "{\"error\":\"wifi_credentials_invalid\"}");
          return;
        }
        uint32_t id;
        const WifiSetupCommand command = reset ? WifiSetupCommand::Reset : WifiSetupCommand::Switch;
        if (!wifiQueueSetup(command, credentials, id)) {
          request->send(409, "application/json", "{\"error\":\"wifi_busy\"}");
          return;
        }
        wifiSendAccepted(request, id, credentials.ssid);
      });
  handler->setMethod(HTTP_POST);
  handler->setMaxContentLength(1024);

  server.on("/setup/wifi/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!wifiRequestOriginAllowed(request)) {
      request->send(403, "application/json", "{\"error\":\"origin_denied\"}");
      return;
    }
    uint32_t id;
    if (!wifiQueueSetup(WifiSetupCommand::Scan, {}, id)) {
      request->send(409, "application/json", "{\"error\":\"wifi_busy\"}");
      return;
    }
    wifiSendAccepted(request, id, "");
  });

  server.on("/setup/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    const WifiScanResult scan = wifiReadScanResult();
    JsonDocument response;
    response["state"] = scan.state == WifiScanState::Running ? "scanning" :
                        scan.state == WifiScanState::Complete ? "complete" :
                        scan.state == WifiScanState::Failed ? "failed" : "idle";
    JsonArray networks = response["networks"].to<JsonArray>();
    for (uint8_t index = 0; index < scan.count; ++index) {
      JsonObject network = networks.add<JsonObject>();
      network["ssid"] = scan.networks[index].ssid;
      network["rssi"] = scan.networks[index].rssi;
      network["secure"] = scan.networks[index].secure;
    }
    wifiSendJson(request, 200, response);
  });

  server.on("/setup/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    const WifiSetupStatus status = wifiReadSetupStatus();
    JsonDocument response;
    response["operation_id"] = status.operationId;
    response["state"] = wifiSetupPhaseName(status.phase);
    response["error"] = wifiSetupErrorName(status.error);
    response["requested_ssid"] = status.requestedSsid;
    response["ssid"] = status.ssid;
    response["ip"] = status.ip;
    response["connected"] = status.connected;
    response["access_point"] = status.accessPoint;
    response["credentials_saved"] = status.credentialsSaved;
    response["busy"] = wifiSetupBusy();
    response["mdns_name"] = wifiDeviceName();
    response["mdns_available"] = HDS_FEATURE_MDNS != 0;
    wifiSendJson(request, 200, response);
  });
  server.addHandler(handler);
}

#endif
