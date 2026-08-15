import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8")


def check_no_hardcoded_identity(wifi_source):
    if re.search(r'MDNS\s*\.\s*begin\s*\(\s*"', wifi_source):
        raise AssertionError("src/wifi_setup.cpp still passes a literal hostname to MDNS.begin")
    if re.search(r'setHostname\s*\(\s*"', wifi_source):
        raise AssertionError("src/wifi_setup.cpp still passes a literal DHCP hostname")
    if "params.getMdnsName()" not in wifi_source:
        raise AssertionError("src/wifi_setup.cpp does not derive its identity from the stored name")
    if 'MDNS.begin(name)' not in wifi_source:
        raise AssertionError("setupMdns() does not start the responder with the stored name")
    if 'MDNS.setInstanceName("Half Decent Scale")' not in wifi_source:
        raise AssertionError("default instance name must stay exactly 'Half Decent Scale'")
    if '"Half Decent Scale (%s)"' not in wifi_source:
        raise AssertionError("renamed scales must advertise 'Half Decent Scale (<name>)'")
    if 'addServiceTxt("decentscale", "tcp", "name", name)' not in wifi_source:
        raise AssertionError("missing DNS-SD TXT record for the device name")


def check_shutdown_withdraws_mdns(wifi_source):
    if "MDNS.end()" not in wifi_source:
        raise AssertionError("src/wifi_setup.cpp must withdraw the mDNS registration")
    withdraw = wifi_source.index("static void mdnsWithdraw()")
    body = wifi_source[withdraw:wifi_source.index("void stopWifi()")]
    if "MDNS.end()" not in body:
        raise AssertionError("mdnsWithdraw() must call MDNS.end() to emit the DNS-SD goodbye")
    stop = wifi_source[wifi_source.index("void stopWifi()"):]
    stop = stop[:stop.index("\n}\n")]
    if "mdnsWithdraw();" not in stop:
        raise AssertionError("stopWifi() must withdraw mDNS before powering the radio down")
    if stop.index("mdnsWithdraw();") > stop.index("WiFi.mode(WIFI_OFF)"):
        raise AssertionError("the goodbye must be sent while the radio is still up")


def check_name_rules(name_header):
    if 'MDNS_NAME_DEFAULT = "hds"' not in name_header:
        raise AssertionError("default device name must be 'hds' so unrenamed scales keep hds.local")
    if "MDNS_NAME_MAX_CHARS = 24" not in name_header:
        raise AssertionError("device name length limit changed without updating the contract")
    for symbol in ("mdnsNameNormalize", "mdnsNameIsDefault", "mdnsNameDefault"):
        if symbol not in name_header:
            raise AssertionError(f"include/mdns_name.h missing {symbol}")
    if "Arduino.h" in name_header or "String" in name_header:
        raise AssertionError("include/mdns_name.h must stay Arduino-free for the native test env")


def check_storage_ownership(wifi_source, storage_header):
    if 'wifiMdnsNameKey = "mdns_name"' not in wifi_source:
        raise AssertionError("device name key must be 'mdns_name' in the wifi namespace")
    if "mdns_name" in storage_header:
        raise AssertionError("device name must not leak into the hds settings schema")


def check_endpoint(webserver_source):
    if '"/setup/name"' not in webserver_source:
        raise AssertionError("include/webserver.h missing the /setup/name handler")
    if "saveDeviceNameForRestart" not in webserver_source:
        raise AssertionError("/setup/name must persist through saveDeviceNameForRestart")
    if "remoteQueueResetAt" not in webserver_source:
        raise AssertionError("/setup/name must queue the restart that applies the new identity")
    handler = webserver_source[webserver_source.index('"/setup/name"'):]
    handler = handler[:handler.index("nameHandler->setMaxContentLength")]
    if re.search(r"\bMDNS\s*\.", handler):
        raise AssertionError("/setup/name must not touch mDNS from the AsyncTCP task")
    if "strcmp(normalized, wifiDeviceName())" not in handler:
        raise AssertionError("/setup/name must compare against the current name before restarting")
    if handler.index("strcmp(normalized, wifiDeviceName())") > handler.index("saveDeviceNameForRestart"):
        raise AssertionError("the unchanged-name check must run before the NVS write")
    if '\\"restarting\\":false' not in handler or '\\"restarting\\":true' not in handler:
        raise AssertionError("/setup/name must report whether it is restarting")


def check_device_screen(menu_source):
    if "wifiDeviceName()" not in menu_source:
        raise AssertionError("include/menu.h must show the device name on the WiFi status screen")


def check_ci(nightly_workflow):
    if "tools/test_mdns_name_contract.py" not in nightly_workflow:
        raise AssertionError("nightly.yml must run the mdns name contract check")
    if "pio test -e native" not in nightly_workflow:
        raise AssertionError("nightly.yml must run the native unit tests")


def check_status_frame(websocket_source):
    if '\\"mdns_name\\":\\"%s\\"' not in websocket_source:
        raise AssertionError("status frame missing the mdns_name field")
    if "wifiDeviceName()," not in websocket_source:
        raise AssertionError("status frame does not pass the effective device name")


def check_web_ui(index_html):
    if "/setup/name" not in index_html:
        raise AssertionError("default web app index.html does not post to /setup/name")
    for element in ("name-form", "device-name", "setting-name", "name-status"):
        if f'id="{element}"' not in index_html:
            raise AssertionError(f"default web app index.html missing element id {element}")
    if "status.mdns_name" not in index_html:
        raise AssertionError("default web app index.html does not render mdns_name from the status frame")
    if "deviceNamePrefilled" not in index_html:
        raise AssertionError("the name field must prefill once, not on every status poll")
    if "body.restarting" not in index_html:
        raise AssertionError("the UI must distinguish a rename from an unchanged-name response")


def main():
    wifi_source = read("src/wifi_setup.cpp")
    check_no_hardcoded_identity(wifi_source)
    check_shutdown_withdraws_mdns(wifi_source)
    check_name_rules(read("include/mdns_name.h"))
    check_storage_ownership(wifi_source, read("include/storage.h"))
    check_endpoint(read("include/webserver.h"))
    check_status_frame(read("include/websocket.h"))
    check_web_ui(read("plugins/default-web-apps/assets/index.html"))
    check_device_screen(read("include/menu.h"))
    check_ci(read(".github/workflows/nightly.yml"))
    print("mdns name contract checks passed")


if __name__ == "__main__":
    main()
