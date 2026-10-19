from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    reconnect = block_after((ROOT / "include/wifi_setup_routes.h").read_text(encoding="utf-8"),
                            'server.on("/setup/wifi/continue", HTTP_GET, [](AsyncWebServerRequest *request) {')
    source = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
struct String : std::string {
  using std::string::string;
  String(const std::string &value) : std::string(value) {}
  String(uint32_t value) : std::string(std::to_string(value)) {}
};
struct Parameter {
  String text;
  String value() const { return text; }
};
struct AsyncWebServerResponse {
  int code = 0;
  std::map<String, String> headers;
  void addHeader(const char *name, const String &value) { headers[name] = value; }
};
struct AsyncWebServerRequest {
  std::map<String, Parameter> params;
  AsyncWebServerResponse response;
  int responseCode = 0;
  String responseBody;
  bool hasParam(const char *name) const { return params.count(name) != 0; }
  const Parameter *getParam(const char *name) const { return &params.at(name); }
  AsyncWebServerResponse *beginResponse(int code, const char *, const char *) {
    response.code = code;
    return &response;
  }
  void send(AsyncWebServerResponse *value) { responseCode = value->code; }
  void send(int code, const char *, const char *body) { responseCode = code; responseBody = body; }
};
struct WifiSetupStatus { uint32_t operationId = 9; } status;
WifiSetupStatus wifiReadSetupStatus() { return status; }
String wifiSetupDeviceId() { return "111111111111"; }
void continueWifi(AsyncWebServerRequest *request) { @RECONNECT@ }

int main() {
  AsyncWebServerRequest request;
  continueWifi(&request);
  assert(request.responseCode == 409);
  request.params["wifi_device"] = Parameter{"222222222222"};
  continueWifi(&request);
  assert(request.responseCode == 409);
  request.params["wifi_device"] = Parameter{"111111111111"};
  continueWifi(&request);
  assert(request.responseCode == 302);
  assert(request.response.headers["Location"] == "/?wifi_device=111111111111");
  assert(request.response.headers["Cache-Control"] == "no-store");
  for (const uint32_t id : {9u, 2147483648u, 4294967295u}) {
    status.operationId = id;
    request.params["wifi_attempt"] = Parameter{String(id)};
    continueWifi(&request);
    assert(request.responseCode == 302);
    assert(request.response.headers["Location"] == "/?wifi_device=111111111111&wifi_attempt=" + String(id));
  }
  status.operationId = 9;
  for (const char *attempt : {"", "0", "09", "9 ", "+9", "-9", "1e1", "10", "4294967296", "9999999999999999999999999"}) {
    request.params["wifi_attempt"] = Parameter{attempt};
    continueWifi(&request);
    assert(request.responseCode == 409);
    assert(request.responseBody.find("no longer current") != std::string::npos);
  }
}
'''.replace("@RECONNECT@", reconnect)
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        cpp, binary = Path(directory) / "reconnect.cpp", Path(directory) / "reconnect.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("WiFi reconnect identity, full uint32 operation ID, redirect context and stale/invalid guards passed")


if __name__ == "__main__":
    main()
