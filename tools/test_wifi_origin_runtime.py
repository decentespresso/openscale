from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    control = (ROOT / "src/wifi_setup_control.cpp").read_text(encoding="utf-8")
    routes = (ROOT / "include/wifi_setup_routes.h").read_text(encoding="utf-8")
    source = r'''
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
struct String : std::string {
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
  void toLowerCase() {
    for (char &value : *this) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  }
  bool endsWith(const char *suffix) const {
    const size_t length = strlen(suffix);
    return size() >= length && compare(size() - length, length, suffix) == 0;
  }
  void remove(size_t index) { erase(index); }
  void toCharArray(char *output, size_t capacity) const {
    assert(size() < capacity);
    memcpy(output, c_str(), size() + 1);
  }
};
using portMUX_TYPE = int;
using SemaphoreHandle_t = int *;
using StaticSemaphore_t = int;
using TaskHandle_t = void *;
#include "wifi_setup.h"
portMUX_TYPE wifiSetupMux = 0;
void portENTER_CRITICAL(int *mux) { assert(*mux == 0); *mux = 1; }
void portEXIT_CRITICAL(int *mux) { assert(*mux == 1); *mux = 0; }
volatile WifiSetupStatus wifiSetupStatus;
volatile WifiSetupRequest wifiPendingRequest;
WifiSetupRuntime wifiSetupRuntime;
constexpr int WL_CONNECTED = 3, WIFI_AP = 2;
struct Ip {
  operator uint32_t() const { return 1; }
  String toString() const { return "192.168.50.30"; }
};
struct {
  int status() const { return WL_CONNECTED; }
  int getMode() const { return 1; }
  Ip localIP() const { return {}; }
  Ip softAPIP() const { return {}; }
  String SSID() const { return "Network"; }
} WiFi;
struct {
  String getSSID() const { return "Network"; }
  bool hasCredentials() const { return true; }
} params;
const char *wifiDeviceName() { return "hds"; }
#if HDS_FEATURE_MDNS
constexpr int ESP_OK = 0;
std::string responderName = "hds-2";
int mdnsCalls = 0, mdnsResult = ESP_OK;
int mdns_hostname_get(char *output) {
  assert(wifiSetupMux == 0);
  ++mdnsCalls;
  assert(responderName.size() < MDNS_NAME_BUF_LEN);
  memcpy(output, responderName.c_str(), responderName.size() + 1);
  return mdnsResult;
}
#endif
void wifiPublishSetupStatus() { @PUBLISH@ }
WifiSetupStatus wifiReadSetupStatus() { @READ@ }
struct Header {
  String text;
  String value() const { return text; }
};
struct AsyncWebServerRequest {
  String hostname;
  std::map<String, Header> headers;
  String host() const { return hostname; }
  bool hasHeader(const char *name) const { return headers.count(name) != 0; }
  const Header *getHeader(const char *name) const { return &headers.at(name); }
};
static bool wifiRequestOriginAllowed(AsyncWebServerRequest *request) { @ORIGIN@ }
bool originAllowed(const char *host, const char *origin, const char *site = "same-origin") {
  AsyncWebServerRequest request;
  request.hostname = host;
  if (origin != nullptr) request.headers["Origin"] = Header{origin};
  if (site != nullptr) request.headers["Sec-Fetch-Site"] = Header{site};
  return wifiRequestOriginAllowed(&request);
}
int main() {
  wifiPublishSetupStatus();
  assert(originAllowed("hds", "http://hds"));
  assert(originAllowed("hds.local", "http://hds.local"));
  assert(originAllowed("192.168.50.30", "http://192.168.50.30"));
  assert(originAllowed("HDS.LOCAL:80", "http://HDS.LOCAL:80"));
  assert(originAllowed("hds.local", nullptr, nullptr));
  assert(!originAllowed("hds.local", "http://unrelated.local"));
  assert(!originAllowed("hds.local", "https://hds.local"));
  assert(!originAllowed("hds.local", "http://hds.local", "cross-site"));
  assert(!originAllowed("unrelated.local", "http://unrelated.local"));
  assert(!originAllowed("hds-3.local", "http://hds-3.local"));
#if HDS_FEATURE_MDNS
  assert(mdnsCalls == 1);
  assert(strcmp(wifiReadSetupStatus().mdnsHostname, "hds-2") == 0);
  assert(originAllowed("hds-2", "http://hds-2"));
  assert(originAllowed("HDS-2.LOCAL:80", "http://HDS-2.LOCAL:80"));
  assert(!originAllowed("hds-2.local", "http://hds.local"));
  assert(!originAllowed("hds-2.local", "http://hds-2.local", "cross-site"));
  assert(mdnsCalls == 1);
  responderName = "abcdefghijklmnopqrstuvwx-2";
  wifiPublishSetupStatus();
  assert(strcmp(wifiReadSetupStatus().mdnsHostname, responderName.c_str()) == 0);
  assert(originAllowed("abcdefghijklmnopqrstuvwx-2.local", "http://abcdefghijklmnopqrstuvwx-2.local"));
  assert(!originAllowed("hds-2.local", "http://hds-2.local"));
  mdnsResult = 1;
  wifiPublishSetupStatus();
  assert(wifiReadSetupStatus().mdnsHostname[0] == 0);
  assert(!originAllowed("abcdefghijklmnopqrstuvwx-2.local", "http://abcdefghijklmnopqrstuvwx-2.local"));
  assert(originAllowed("hds.local", "http://hds.local"));
  assert(originAllowed("192.168.50.30", "http://192.168.50.30"));
#else
  assert(!originAllowed("hds-2.local", "http://hds-2.local"));
#endif
}
'''
    source = source.replace("@PUBLISH@", block_after(control, "void wifiPublishSetupStatus() {"))
    source = source.replace("@READ@", block_after(control, "WifiSetupStatus wifiReadSetupStatus() {"))
    source = source.replace("@ORIGIN@", block_after(
        routes, "static bool wifiRequestOriginAllowed(AsyncWebServerRequest *request) {"))
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "Arduino.h").write_text("", encoding="utf-8")
        (root / "mdns.h").write_text("#define MDNS_NAME_BUF_LEN 69\n", encoding="utf-8")
        cpp = root / "runtime.cpp"
        cpp.write_text(source, encoding="utf-8")
        for mdns in (0, 1):
            binary = root / f"runtime-{mdns}.exe"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            f"-DHDS_FEATURE_MDNS={mdns}", "-I", str(root), "-I", str(ROOT / "include"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("WiFi effective mDNS hostname snapshots, origin guards, and mDNS-disabled runtime checks passed")


if __name__ == "__main__":
    main()
