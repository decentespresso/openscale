from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    routes = (ROOT / "include/wifi_setup_routes.h").read_text(encoding="utf-8")
    source = r'''
#include <cassert>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include "wifi_credentials.h"
struct JsonString {
  const char *text;
  size_t length;
  const char *c_str() const { return text; }
  size_t size() const { return length; }
};
struct JsonValue {
  std::variant<std::monostate, std::string, bool, int> data;
  JsonValue() = default;
  JsonValue(const char *value) : data(std::string(value)) {}
  JsonValue(std::string value) : data(value) {}
  JsonValue(bool value) : data(value) {}
  JsonValue(int value) : data(value) {}
  bool isNull() const { return std::holds_alternative<std::monostate>(data); }
  template<class T> bool is() const {
    if constexpr (std::is_same_v<T, const char *>) return std::holds_alternative<std::string>(data);
    else return std::holds_alternative<T>(data);
  }
  template<class T> T as() const {
    const auto *text = std::get_if<std::string>(&data);
    return {text ? text->c_str() : nullptr, text ? text->size() : 0};
  }
  bool operator|(bool fallback) const {
    const auto *value = std::get_if<bool>(&data);
    return value ? *value : fallback;
  }
};
struct JsonObject {
  const std::map<std::string, JsonValue> *fields;
  bool isNull() const { return fields == nullptr; }
  const JsonValue &operator[](const char *key) const {
    static const JsonValue empty;
    const auto found = fields->find(key);
    return found == fields->end() ? empty : found->second;
  }
};
struct JsonVariant {
  std::map<std::string, JsonValue> fields;
  template<class T> T as() const { return {&fields}; }
};
bool scanComplete = false;
bool wifiScannedSsid(const char *ssid, size_t length) {
  return scanComplete && length == 6 && memcmp(ssid, " Cafe ", length) == 0;
}
static bool parseWifiSetupCredentials(JsonVariant &json, WifiCredentials &credentials, bool &reset) {
  @PARSE@
}
int main() {
  JsonVariant json{{{"ssid", " Cafe "}, {"pass", " password "}}};
  WifiCredentials credentials;
  bool reset = false;
  assert(parseWifiSetupCredentials(json, credentials, reset) && !reset);
  assert(strcmp(credentials.ssid, "Cafe") == 0 && strcmp(credentials.pass, "password") == 0);
  json.fields["preserve_edges"] = true;
  assert(parseWifiSetupCredentials(json, credentials, reset) && !reset);
  assert(strcmp(credentials.ssid, " Cafe ") == 0 && strcmp(credentials.pass, " password ") == 0);
  json.fields["preserve_edges"] = false;
  assert(parseWifiSetupCredentials(json, credentials, reset));
  assert(strcmp(credentials.ssid, "Cafe") == 0 && strcmp(credentials.pass, "password") == 0);
  json.fields["preserve_edges"] = "true";
  assert(!parseWifiSetupCredentials(json, credentials, reset));
  json.fields["preserve_edges"] = 1;
  assert(!parseWifiSetupCredentials(json, credentials, reset));
  json.fields["preserve_edges"] = true;
  json.fields["scanned"] = true;
  assert(!parseWifiSetupCredentials(json, credentials, reset));
  scanComplete = true;
  assert(parseWifiSetupCredentials(json, credentials, reset));
  assert(strcmp(credentials.ssid, " Cafe ") == 0);
  json.fields["ssid"] = "Cafe";
  assert(!parseWifiSetupCredentials(json, credentials, reset));
  json.fields.erase("scanned");
  json.fields["pass"] = "short   ";
  assert(parseWifiSetupCredentials(json, credentials, reset));
  assert(strcmp(credentials.pass, "short   ") == 0);
  json.fields["preserve_edges"] = false;
  assert(!parseWifiSetupCredentials(json, credentials, reset));
  json.fields["preserve_edges"] = true;
  for (const auto &password : {std::string("short"), std::string("password\t"),
       std::string("pass\0word", 9), std::string(65, 'a'), std::string("bad\xc0\xafpass")}) {
    json.fields["pass"] = password;
    assert(!parseWifiSetupCredentials(json, credentials, reset));
  }
  json.fields["pass"] = "password";
  for (const auto &ssid : {std::string(""), std::string("Cafe\t"), std::string("Ca\0fe", 5),
       std::string(33, 'a'), std::string("bad\xc0\xaf")}) {
    json.fields["ssid"] = ssid;
    assert(!parseWifiSetupCredentials(json, credentials, reset));
  }
  json.fields["ssid"] = "";
  json.fields["pass"] = "";
  assert(parseWifiSetupCredentials(json, credentials, reset) && reset);
}
'''
    source = source.replace("@PARSE@", block_after(
        routes, "static bool parseWifiSetupCredentials(JsonVariant &json, WifiCredentials &credentials,"))
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp = root / "runtime.cpp"
        binary = root / "runtime.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("WiFi exact manual credentials, legacy trimming, scan provenance, validation, and reset runtime checks passed")


if __name__ == "__main__":
    main()
