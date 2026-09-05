#ifndef WIFI_CONFIG_SERVER_H
#define WIFI_CONFIG_SERVER_H

#include "config.h"
#if HDS_FEATURE_WIFI && !HDS_FEATURE_WEBSERVER
#include "mdns_name.h"
#include "parameter.h"
#include "wifi_setup.h"
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const size_t WIFI_CONFIG_BUFFER_MAX_BYTES = 320;
static const unsigned long WIFI_CONFIG_CLIENT_TIMEOUT_MS = 2000;
static const unsigned long WIFI_CONFIG_REQUEST_TIMEOUT_MS = 5000;
static const unsigned long WIFI_CONFIG_RESTART_DELAY_MS = 500;

static const char WIFI_CONFIG_PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OpenScale setup</title><style>body{font:16px sans-serif;max-width:30rem;margin:2rem auto;padding:0 1rem}label,input,button{display:block;width:100%;box-sizing:border-box;margin:.5rem 0}input,button{padding:.7rem}</style>
<h1>OpenScale setup</h1>
<form method="post" action="/setup/wifi"><label>WiFi name<input name="ssid" maxlength="32" required></label><label>Password<input name="pass" type="password" maxlength="64"></label><button>Save WiFi</button></form>
<form method="post" action="/setup/name"><label>Device name<input name="name" maxlength="24" required></label><button>Save name</button></form>
</html>)html";

static int wifiConfigServerSocket = -1;
static int wifiConfigClientSocket = -1;
enum WifiConfigRoute : uint8_t {
  WIFI_CONFIG_ROUTE_INVALID,
  WIFI_CONFIG_ROUTE_ROOT,
  WIFI_CONFIG_ROUTE_WIFI,
  WIFI_CONFIG_ROUTE_NAME
};
enum WifiConfigPhase : uint8_t {
  WIFI_CONFIG_PHASE_REQUEST,
  WIFI_CONFIG_PHASE_HEADERS,
  WIFI_CONFIG_PHASE_BODY
};
static char wifiConfigBuffer[WIFI_CONFIG_BUFFER_MAX_BYTES + 1];
static char wifiConfigHost[64];
static char wifiConfigOriginHost[64];
static size_t wifiConfigBufferBytes = 0;
static size_t wifiConfigBodyBytes = 0;
static unsigned long wifiConfigLastByteAt = 0;
static unsigned long wifiConfigRequestStartedAt = 0;
static unsigned long wifiConfigLastStartAttemptAt = 0;
static bool wifiConfigContentLengthSeen = false;
static bool wifiConfigContentTypeAllowed = false;
static bool wifiConfigOriginSeen = false;
static WifiConfigRoute wifiConfigRoute = WIFI_CONFIG_ROUTE_INVALID;
static WifiConfigPhase wifiConfigPhase = WIFI_CONFIG_PHASE_REQUEST;

static bool wifiConfigWrite(const char *data, size_t dataBytes) {
  size_t sentBytes = 0;
  const unsigned long startedAt = millis();
  while (sentBytes < dataBytes) {
    const int result = send(wifiConfigClientSocket, data + sentBytes,
                            dataBytes - sentBytes, 0);
    if (result > 0) {
      sentBytes += (size_t)result;
      continue;
    }
    if ((errno != EAGAIN && errno != EWOULDBLOCK) ||
        millis() - startedAt >= 20) {
      return false;
    }
    delay(1);
  }
  return true;
}

static void wifiConfigSend(const char *status, const char *contentType,
                           const char *body, size_t bodyBytes) {
  char header[320];
  const int headerBytes = snprintf(
      header, sizeof(header),
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'\r\nConnection: close\r\n\r\n",
      status, contentType, (unsigned)bodyBytes);
  if (headerBytes <= 0 || (size_t)headerBytes >= sizeof(header)) {
    return;
  }
  wifiConfigWrite(header, (size_t)headerBytes);
  wifiConfigWrite(body, bodyBytes);
}

static void wifiConfigSendText(const char *status, const char *body) {
  wifiConfigSend(status, "text/plain; charset=utf-8", body, strlen(body));
}

static void wifiConfigCloseClient() {
  if (wifiConfigClientSocket >= 0) {
    shutdown(wifiConfigClientSocket, SHUT_WR);
    close(wifiConfigClientSocket);
    wifiConfigClientSocket = -1;
  }
  wifiConfigBufferBytes = 0;
  wifiConfigBodyBytes = 0;
  wifiConfigBuffer[0] = 0;
  wifiConfigHost[0] = 0;
  wifiConfigOriginHost[0] = 0;
  wifiConfigContentLengthSeen = false;
  wifiConfigContentTypeAllowed = false;
  wifiConfigOriginSeen = false;
  wifiConfigRoute = WIFI_CONFIG_ROUTE_INVALID;
  wifiConfigPhase = WIFI_CONFIG_PHASE_REQUEST;
}

static int wifiConfigHex(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

static bool wifiConfigDecodeValue(const char *encoded, size_t encodedBytes,
                                  char *decoded, size_t decodedBytes) {
  size_t outputBytes = 0;
  for (size_t index = 0; index < encodedBytes; index++) {
    char value = encoded[index];
    if (value == '+') {
      value = ' ';
    } else if (value == '%') {
      if (index + 2 >= encodedBytes) {
        return false;
      }
      const int high = wifiConfigHex(encoded[index + 1]);
      const int low = wifiConfigHex(encoded[index + 2]);
      if (high < 0 || low < 0) {
        return false;
      }
      value = (char)((high << 4) | low);
      index += 2;
    }
    if (value == 0 || outputBytes + 1 >= decodedBytes) {
      return false;
    }
    decoded[outputBytes++] = value;
  }
  decoded[outputBytes] = 0;
  return true;
}

static bool wifiConfigFormValue(const char *body, size_t bodyBytes,
                                const char *name, char *value,
                                size_t valueBytes) {
  const size_t nameBytes = strlen(name);
  size_t pairStart = 0;
  while (pairStart <= bodyBytes) {
    size_t pairEnd = pairStart;
    while (pairEnd < bodyBytes && body[pairEnd] != '&') {
      pairEnd++;
    }
    size_t separator = pairStart;
    while (separator < pairEnd && body[separator] != '=') {
      separator++;
    }
    if (separator - pairStart == nameBytes &&
        strncmp(body + pairStart, name, nameBytes) == 0) {
      const size_t encodedStart = separator < pairEnd ? separator + 1 : pairEnd;
      return wifiConfigDecodeValue(body + encodedStart, pairEnd - encodedStart,
                                   value, valueBytes);
    }
    if (pairEnd == bodyBytes) {
      break;
    }
    pairStart = pairEnd + 1;
  }
  return false;
}

static void wifiConfigSaveWifi(const char *body, size_t bodyBytes) {
  char ssid[33] = {0};
  char pass[65] = {0};
  if (!wifiConfigFormValue(body, bodyBytes, "ssid", ssid, sizeof(ssid))) {
    wifiConfigSendText("400 Bad Request", "Invalid WiFi settings");
    return;
  }
  wifiConfigFormValue(body, bodyBytes, "pass", pass, sizeof(pass));
  if (!saveCredentialsForRestart(String(ssid), String(pass))) {
    wifiConfigSendText("500 Internal Server Error", "WiFi settings were not saved");
    return;
  }
  wifiConfigSendText("200 OK", "Saved. Restarting.");
  remoteQueueResetAt(millis() + WIFI_CONFIG_RESTART_DELAY_MS);
}

static void wifiConfigSaveName(const char *body, size_t bodyBytes) {
  char requested[MDNS_NAME_BUFFER_BYTES] = {0};
  char normalized[MDNS_NAME_BUFFER_BYTES] = {0};
  if (!wifiConfigFormValue(body, bodyBytes, "name", requested,
                           sizeof(requested)) ||
      !mdnsNameNormalize(requested, normalized, sizeof(normalized))) {
    wifiConfigSendText("400 Bad Request", "Invalid device name");
    return;
  }
  if (strcmp(normalized, wifiDeviceName()) == 0) {
    wifiConfigSendText("200 OK", "Device name unchanged");
    return;
  }
  char stored[MDNS_NAME_BUFFER_BYTES] = {0};
  if (!saveDeviceNameForRestart(normalized, stored, sizeof(stored))) {
    wifiConfigSendText("500 Internal Server Error", "Device name was not saved");
    return;
  }
  wifiConfigSendText("200 OK", "Saved. Restarting.");
  remoteQueueResetAt(millis() + WIFI_CONFIG_RESTART_DELAY_MS);
}

static bool wifiConfigLineStarts(const char *prefix) {
  const size_t prefixBytes = strlen(prefix);
  return wifiConfigBufferBytes >= prefixBytes &&
         strncasecmp(wifiConfigBuffer, prefix, prefixBytes) == 0;
}

static void wifiConfigCopyLineValue(const char *prefix, char *value,
                                    size_t valueBytes) {
  const size_t prefixBytes = strlen(prefix);
  const size_t sourceBytes = wifiConfigBufferBytes - prefixBytes;
  if (sourceBytes + 1 > valueBytes) {
    value[0] = 0;
    return;
  }
  memcpy(value, wifiConfigBuffer + prefixBytes, sourceBytes);
  value[sourceBytes] = 0;
}

static void wifiConfigReadRequestLine() {
  if (strcmp(wifiConfigBuffer, "GET / HTTP/1.1") == 0 ||
      strcmp(wifiConfigBuffer, "GET / HTTP/1.0") == 0) {
    wifiConfigRoute = WIFI_CONFIG_ROUTE_ROOT;
  } else if (strcmp(wifiConfigBuffer, "POST /setup/wifi HTTP/1.1") == 0 ||
             strcmp(wifiConfigBuffer, "POST /setup/wifi HTTP/1.0") == 0) {
    wifiConfigRoute = WIFI_CONFIG_ROUTE_WIFI;
  } else if (strcmp(wifiConfigBuffer, "POST /setup/name HTTP/1.1") == 0 ||
             strcmp(wifiConfigBuffer, "POST /setup/name HTTP/1.0") == 0) {
    wifiConfigRoute = WIFI_CONFIG_ROUTE_NAME;
  }
}

static void wifiConfigProcessBody() {
  if (wifiConfigRoute == WIFI_CONFIG_ROUTE_WIFI) {
    wifiConfigSaveWifi(wifiConfigBuffer, wifiConfigBodyBytes);
  } else if (wifiConfigRoute == WIFI_CONFIG_ROUTE_NAME) {
    wifiConfigSaveName(wifiConfigBuffer, wifiConfigBodyBytes);
  } else {
    wifiConfigSendText("404 Not Found", "Not found");
  }
}

static bool wifiConfigFinishHeaders() {
  if (wifiConfigRoute == WIFI_CONFIG_ROUTE_ROOT) {
    wifiConfigSend("200 OK", "text/html; charset=utf-8", WIFI_CONFIG_PAGE,
                   sizeof(WIFI_CONFIG_PAGE) - 1);
    return true;
  }
  if (wifiConfigRoute == WIFI_CONFIG_ROUTE_INVALID) {
    wifiConfigSendText("404 Not Found", "Not found");
    return true;
  }
  if (!wifiConfigContentLengthSeen) {
    wifiConfigSendText("400 Bad Request", "Invalid content length");
    return true;
  }
  if (!wifiConfigContentTypeAllowed) {
    wifiConfigSendText("415 Unsupported Media Type", "Form data required");
    return true;
  }
  if (wifiConfigOriginSeen &&
      (wifiConfigHost[0] == 0 || wifiConfigOriginHost[0] == 0 ||
       strcmp(wifiConfigHost, wifiConfigOriginHost) != 0)) {
    wifiConfigSendText("403 Forbidden", "Cross-origin request rejected");
    return true;
  }
  wifiConfigPhase = WIFI_CONFIG_PHASE_BODY;
  wifiConfigBufferBytes = 0;
  if (wifiConfigBodyBytes == 0) {
    wifiConfigBuffer[0] = 0;
    wifiConfigProcessBody();
    return true;
  }
  return false;
}

static bool wifiConfigReadHeaderLine() {
  if (wifiConfigBufferBytes == 0) {
    return wifiConfigFinishHeaders();
  }
  if (wifiConfigLineStarts("Host: ")) {
    wifiConfigCopyLineValue("Host: ", wifiConfigHost, sizeof(wifiConfigHost));
  } else if (wifiConfigLineStarts("Origin: ")) {
    wifiConfigOriginSeen = true;
    if (wifiConfigLineStarts("Origin: http://")) {
      wifiConfigCopyLineValue("Origin: http://", wifiConfigOriginHost,
                              sizeof(wifiConfigOriginHost));
    }
  } else if (wifiConfigLineStarts("Content-Length: ")) {
    const char *value = wifiConfigBuffer + strlen("Content-Length: ");
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end != value && end[0] == 0 &&
        parsed <= WIFI_CONFIG_BUFFER_MAX_BYTES) {
      wifiConfigBodyBytes = (size_t)parsed;
      wifiConfigContentLengthSeen = true;
    }
  } else if (wifiConfigLineStarts("Content-Type: ")) {
    const char *value = wifiConfigBuffer + strlen("Content-Type: ");
    wifiConfigContentTypeAllowed =
        strncasecmp(value, "application/x-www-form-urlencoded", 33) == 0;
  }
  return false;
}

static bool wifiConfigConsumeByte(char value) {
  if (wifiConfigPhase == WIFI_CONFIG_PHASE_BODY) {
    wifiConfigBuffer[wifiConfigBufferBytes++] = value;
    if (wifiConfigBufferBytes == wifiConfigBodyBytes) {
      wifiConfigBuffer[wifiConfigBufferBytes] = 0;
      wifiConfigProcessBody();
      return true;
    }
    return false;
  }
  if (value == '\r') {
    return false;
  }
  if (value != '\n') {
    if (wifiConfigBufferBytes == WIFI_CONFIG_BUFFER_MAX_BYTES) {
      wifiConfigSendText("431 Request Header Fields Too Large", "Header line too large");
      return true;
    }
    wifiConfigBuffer[wifiConfigBufferBytes++] = value;
    return false;
  }
  wifiConfigBuffer[wifiConfigBufferBytes] = 0;
  if (wifiConfigPhase == WIFI_CONFIG_PHASE_REQUEST) {
    wifiConfigReadRequestLine();
    wifiConfigPhase = WIFI_CONFIG_PHASE_HEADERS;
    wifiConfigBufferBytes = 0;
    return false;
  }
  const bool complete = wifiConfigReadHeaderLine();
  wifiConfigBufferBytes = 0;
  return complete;
}

static bool wifiConfigStartServer() {
  if (!WiFi.STA.started() && !WiFi.AP.started()) {
    return false;
  }
  const int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (serverSocket < 0) {
    return false;
  }
  int reuseAddress = 1;
  setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddress,
             sizeof(reuseAddress));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(80);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(serverSocket, (sockaddr *)&address, sizeof(address)) != 0 ||
      listen(serverSocket, 1) != 0 ||
      fcntl(serverSocket, F_SETFL, O_NONBLOCK) != 0) {
    close(serverSocket);
    return false;
  }
  wifiConfigServerSocket = serverSocket;
  Serial.println("Mini HTTP setup server started");
  return true;
}

static void wifiConfigServerPoll() {
  if (wifiConfigServerSocket < 0) {
    const unsigned long now = millis();
    if (wifiConfigLastStartAttemptAt != 0 &&
        now - wifiConfigLastStartAttemptAt < 2000) {
      return;
    }
    wifiConfigLastStartAttemptAt = now;
    if (!wifiConfigStartServer()) {
      return;
    }
  }

  if (wifiConfigClientSocket < 0) {
    sockaddr_in clientAddress = {};
    socklen_t clientAddressBytes = sizeof(clientAddress);
    const int clientSocket =
        accept(wifiConfigServerSocket, (sockaddr *)&clientAddress,
               &clientAddressBytes);
    if (clientSocket < 0) {
      return;
    }
    fcntl(clientSocket, F_SETFL, O_NONBLOCK);
    int noDelay = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &noDelay,
               sizeof(noDelay));
    wifiConfigClientSocket = clientSocket;
    wifiConfigLastByteAt = millis();
    wifiConfigRequestStartedAt = wifiConfigLastByteAt;
  }

  uint8_t incoming[64];
  const int received = recv(wifiConfigClientSocket, incoming, sizeof(incoming), 0);
  if (received > 0) {
    wifiConfigLastByteAt = millis();
    for (int index = 0; index < received; index++) {
      if (wifiConfigConsumeByte((char)incoming[index])) {
        wifiConfigCloseClient();
        return;
      }
    }
  } else if (received == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
    wifiConfigCloseClient();
    return;
  }

  if (millis() - wifiConfigLastByteAt >= WIFI_CONFIG_CLIENT_TIMEOUT_MS ||
      millis() - wifiConfigRequestStartedAt >= WIFI_CONFIG_REQUEST_TIMEOUT_MS) {
    wifiConfigSendText("408 Request Timeout", "Request timeout");
    wifiConfigCloseClient();
  }
}

static void stopWifiConfigServer() {
  wifiConfigCloseClient();
  if (wifiConfigServerSocket >= 0) {
    close(wifiConfigServerSocket);
    wifiConfigServerSocket = -1;
  }
}
#endif
#endif
