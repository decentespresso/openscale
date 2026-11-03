import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_auto_off_weight_activity import extract_function


ROOT = Path(__file__).resolve().parents[1]
HARNESS = r"""
#include <cassert>
#include <cmath>
#include <cstdint>
#include "energy_runtime_policy.h"

#define HDS_ENABLE_GRINDER 0
#define USB_DET 4
constexpr int LOW = 0, HIGH = 1, BATTERY_CHARGING = 3;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2;
constexpr int WIFI_STA = 1, WL_CONNECTED = 3;
constexpr float OVER_WEIGHT = 5000;
constexpr int u8g2_font_open_iconic_www_1x_t = 0;
const unsigned char image_battery_0[] = {0};
const unsigned char image_battery_1[] = {1};
const unsigned char image_battery_2[] = {2};
const unsigned char image_battery_3[] = {3};
const unsigned char image_battery_4[] = {4};
const unsigned char image_battery_charging[] = {5};
float f_displayedValue = 0;
bool b_ble_enabled = false, b_wifiEnabled = true, b_is_charging = true;
bool b_shutdownFailBle = false, b_screenFlipped = false, b_debug = false;
bool b_about = false, b_timeOnTop = false, b_adc_recovery_active = false;
bool b_heartBeatIcon = false, usbConnected = false;
unsigned long t_tareStatus = 0, t_shutdownFailBle = 0;
int i_adc_recovery_count = 0, percent = 0;
struct { uint32_t batterySampleSequence = 0; } powerCadence;
struct { unsigned long elapsed() { return 0; } } stopWatch;
struct {
  int connectionStatus = WL_CONNECTED;
  int getMode() { return WIFI_STA; }
  int status() { return connectionStatus; }
} WiFi;
struct {
  const unsigned char *bitmap = nullptr;
  int glyphs = 0;
  void setFont(int) {}
  void drawXBM(int, int, int, int, const unsigned char *image) {
    assert(bitmap == nullptr);
    bitmap = image;
  }
  void drawGlyph(int, int, int) { ++glyphs; }
} u8g2;
int digitalRead(int pin) {
#ifdef V8_1
  return pin == USB_DET && usbConnected ? LOW : HIGH;
#else
  return (pin == USB_DET || pin == BATTERY_CHARGING) && usbConnected ? LOW : HIGH;
#endif
}
bool bleHasLiveClient() { return false; }
int batteryPercent() { return percent; }

FUNCTIONS

void checkFrame(unsigned long now, const unsigned char *expected, int wifiGlyphs) {
  u8g2.bitmap = nullptr;
  u8g2.glyphs = 0;
  drawBattery(now);
  assert(u8g2.bitmap == expected);
  assert(u8g2.glyphs == wifiGlyphs);
  assert(b_is_charging == usbConnected);
}

int main() {
  constexpr int wifiGlyphs = HDS_FEATURE_WIFI ? 1 : 0;
  const uint32_t unknownSignature = energyDisplaySignature(4000);
  assert(unknownSignature == energyDisplaySignature(4500));
  checkFrame(4000, nullptr, wifiGlyphs);
  checkFrame(4500, nullptr, wifiGlyphs);
  percent = 100;
  assert(unknownSignature == energyDisplaySignature(4500));
  checkFrame(4000, nullptr, wifiGlyphs);
  checkFrame(4500, nullptr, wifiGlyphs);

  usbConnected = true;
  checkFrame(4000, image_battery_charging, wifiGlyphs);
  checkFrame(4500, image_battery_charging, wifiGlyphs);
  assert(energyDisplaySignature(4000) == energyDisplaySignature(4500));
  assert(unknownSignature != energyDisplaySignature(4000));
  usbConnected = false;
  percent = 0;

  OledFrameGate redraw;
  assert(redraw.shouldRender(true, unknownSignature, 4500));
  powerCadence.batterySampleSequence = 1;
  const uint32_t emptySignature = energyDisplaySignature(4500);
  assert(emptySignature != unknownSignature);
  assert(redraw.shouldRender(true, emptySignature, 4500));
  assert(energyDisplaySignature(4000) != emptySignature);
  checkFrame(4000, nullptr, wifiGlyphs);
  checkFrame(4500, image_battery_0, wifiGlyphs);

  const unsigned char *levels[] = {
    image_battery_1, image_battery_2, image_battery_3, image_battery_4
  };
  const int percentages[] = {6, 26, 51, 76};
  for (int index = 0; index < 4; ++index) {
    percent = percentages[index];
    ++powerCadence.batterySampleSequence;
    checkFrame(4000, levels[index], wifiGlyphs);
    checkFrame(4500, levels[index], wifiGlyphs);
    assert(energyDisplaySignature(4000) == energyDisplaySignature(4500));
  }
  checkFrame(9000, image_battery_4, wifiGlyphs);

#if HDS_FEATURE_WIFI
  powerCadence.batterySampleSequence = 0;
  WiFi.connectionStatus = 0;
  checkFrame(4000, nullptr, 0);
  checkFrame(4500, nullptr, 1);
  assert(energyDisplaySignature(4000) != energyDisplaySignature(4500));
  b_wifiEnabled = false;
  checkFrame(4500, nullptr, 0);
  assert(energyDisplaySignature(4000) == energyDisplaySignature(4500));
#endif
}
"""


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        raise SystemExit("A C++ compiler is required (g++, clang++, or CXX).")
    firmware = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    functions = "\n".join(extract_function(firmware, signature) for signature in (
        "uint32_t mixEnergyDisplaySignature(uint32_t signature, uint32_t value)",
        "uint32_t energyDisplaySignature(unsigned long now)",
    ))
    battery_start = firmware.index("void drawBattery(unsigned long now)")
    functions += "\n" + firmware[battery_start:firmware.index("void drawAbout()", battery_start)]
    with tempfile.TemporaryDirectory(prefix="battery-display-startup-") as directory:
        cpp = Path(directory) / "battery_display.cpp"
        binary = Path(directory) / ("battery-display.exe" if os.name == "nt" else "battery-display")
        cpp.write_text(HARNESS.replace("FUNCTIONS", functions), encoding="utf-8")
        for wifi in (0, 1):
            for board in ([], ["-DV8_1"]):
                subprocess.run([
                    compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    f"-DHDS_FEATURE_WIFI={wifi}", *board,
                    "-I", str(ROOT / "include"), str(cpp), "-o", str(binary),
                ], check=True)
                subprocess.run([str(binary)], check=True)
    print("battery startup visibility, valid zero, USB, WiFi, and redraw tests passed")


if __name__ == "__main__":
    main()
