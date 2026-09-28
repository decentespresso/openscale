from pathlib import Path
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    ble = (ROOT / "include/ble.h").read_text(encoding="utf-8")
    ws = (ROOT / "include/websocket.h").read_text(encoding="utf-8")
    hds = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    sleepCommand = ws[ws.index('if (websocketEqualsIgnoreCase(command, "sleep") ||'):]
    loop = block_after(hds, "void loop()")
    sleepGate = loop.index("if (!b_softSleep)")
    wifiGuard = "if (b_softSleep && b_wifiEnabled)"
    assert loop.index(wifiGuard) < sleepGate
    assert loop.index("if (b_ota)") < loop.index(wifiGuard)
    harness = r'''
#include <cassert>
#include <cstdint>
bool b_softSleep = false, b_u8g2Sleep = false, b_wifiEnabled = true;
bool rail = true, accessory = true;
int wakes = 0, supervises = 0, polls = 0;
constexpr uint32_t WSP_SLEEP_OFF = 1, WSP_SLEEP_ON = 2, WSP_DISPLAY_OFF = 4;
constexpr int PWR_CTRL = 3, ACC_PWR_CTRL = 14, LOW = 0;
uint32_t pending = 0;
struct { void setPowerSave(int) {} } u8g2;
struct { void println(const char*) {} } Serial;
void digitalWrite(int pin, int value) { (pin == PWR_CTRL ? rail : accessory) = value; }
void refreshEnergyIdleWakeForRuntimeState() {}
void remoteReplacePending(uint32_t set, uint32_t clear) { pending = (pending & ~clear) | set; }
void wsReplacePending(uint32_t set, uint32_t clear) { remoteReplacePending(set, clear); }
void sendWebsocketStatus(void*, const char*) {}
void wakeScaleFromSoftSleep(const char*) { rail = accessory = true; b_softSleep = false; ++wakes; }
void wifiSupervise() { ++supervises; }
void wifiConfigServerPoll() { ++polls; }
void bleWake() { @BLE_WAKE@ }
bool wsWake() { void *client = nullptr; @WS_WAKE@ }
void sleep() { @SLEEP@ }
void dispatch() { const auto mask = pending; pending = 0; @WAKE@ }
void supervise() { if (b_softSleep && b_wifiEnabled) { @WIFI@ } }
int main() {
  for (int transport = 0; transport < 2; ++transport) {
    sleep();
    assert(b_softSleep && !rail && !accessory);
    if (transport == 0) bleWake(); else assert(wsWake());
    assert(b_softSleep && !rail && !accessory);
    const int before = wakes;
    dispatch();
    assert(!b_softSleep && rail && accessory && wakes == before + 1);
    if (transport == 0) bleWake(); else assert(wsWake());
    dispatch();
    assert(wakes == before + 1);
    pending = WSP_SLEEP_ON;
    if (transport == 0) bleWake(); else assert(wsWake());
    assert(!(pending & WSP_SLEEP_ON));
    dispatch();
    assert(!b_softSleep && rail && accessory);
  }
  b_softSleep = true;
  supervise();
  assert(supervises == 1);
  b_wifiEnabled = false;
  supervise();
  assert(supervises == 1);
}
'''
    replacements = {
        "BLE_WAKE": block_after(ble, "void softSleepOff()"),
        "WS_WAKE": block_after(sleepCommand, 'if (websocketEqualsIgnoreCase(action, "off") ||'),
        "SLEEP": block_after(ws, "if (mask & WSP_SLEEP_ON)"),
        "WAKE": block_after(ws, "if (mask & WSP_SLEEP_OFF)"),
        "WIFI": block_after(loop, wifiGuard),
    }
    for marker, source in replacements.items():
        harness = harness.replace(f"@{marker}@", source)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "check.cpp"
        source.write_text(harness, encoding="utf-8")
        for energy, webserver in ((0, 0), (0, 1), (1, 1)):
            binary = Path(directory) / "check.exe"
            subprocess.run(["g++", "-std=c++17", f"-DHDS_ENABLE_ENERGY_MENU={energy}",
                            f"-DHDS_FEATURE_WEBSERVER={webserver}", str(source),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("Soft-sleep transport dispatch and sleeping WiFi supervision passed")


if __name__ == "__main__":
    main()
