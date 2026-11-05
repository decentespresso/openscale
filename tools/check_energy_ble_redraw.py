from pathlib import Path
import shutil
import subprocess
import tempfile

from test_energy_stage0_contract import body, FIRMWARE, ROOT


def main():
    source = r'''
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include "app_prompt_state.h"
#include "app_prompt_scroll.h"
#include "energy_policy.h"
#include "energy_runtime_policy.h"
#define HDS_ENABLE_ENERGY_MENU 1
#define HDS_FEATURE_WIFI 1
#define USB_DET 4
constexpr int LOW = 0, HIGH = 1, BATTERY_CHARGING = 3;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2;
constexpr int WIFI_STA = 1, WL_CONNECTED = 3, U8G2_R0 = 0, U8G2_R2 = 2;
constexpr int LCDWidth = 128, LCDHeight = 64;
constexpr uint8_t u8g2_font_6x10_tr[] = {0};
constexpr float OVER_WEIGHT = 5000;
float f_batteryVoltage = 3.8, showEmptyBatteryBelowVoltage = 3.3;
float showFullBatteryAboveVoltage = 4.2, f_displayedValue = 0;
bool connected = false, b_ble_enabled = true, b_wifiEnabled = true;
bool b_shutdownFailBle = false, b_screenFlipped = false, b_debug = false;
bool b_about = false, b_timeOnTop = false, b_adc_recovery_active = false;
bool b_heartBeatIcon = false;
bool b_menu = false, b_calibration = false, b_softSleep = false;
bool b_ota = false, b_u8g2Sleep = false;
int GPIO_power_on_with = -1;
AppPromptState promptState;
AppPromptScrollState appPromptScrollState;
AppPromptState snapshotAppPrompt() { return promptState; }
struct { uint32_t batterySampleSequence = 1; } powerCadence;
int batteryPercent() { return 50; }
unsigned long now = 0, t_tareStatus = 0, t_shutdownFailBle = 0;
unsigned long t_oled_refresh = 0, i_oled_print_interval = 100;
int i_adc_recovery_count = 0;
const unsigned char image_ble_enabled[] = {1};
EnergyPolicy energyPolicy;
struct { DisplayIdleMode displayMode = DisplayIdleMode::Active; OledFrameGate oledFrames; } energyRuntime;
struct { unsigned long elapsed() { return 0; } } stopWatch;
struct { int getMode() { return WIFI_STA; } int status() { return WL_CONNECTED; } } WiFi;
struct {
  bool icon = false;
  unsigned frames = 0;
  unsigned bitmapCalls = 0;
  int promptX = 0;
  std::string promptText;
  void firstPage() { icon = false; bitmapCalls = 0; promptText.clear(); frames++; }
  bool nextPage() { return false; }
  void setDisplayRotation(int) {}
  void setFont(const uint8_t *) {}
  void setFontMode(int) {}
  void setDrawColor(int) {}
  int getStrWidth(const char *text) { return static_cast<int>(std::strlen(text)) * 6; }
  void drawStr(int x, int y, const char *text) {
    assert(y == LCDHeight - 16);
    promptX = x;
    promptText = text;
  }
  void drawXBM(int, int, int, int, const unsigned char *image) { bitmapCalls++; icon = image == image_ble_enabled; }
} u8g2;
unsigned long millis() { return now; }
int digitalRead(int) { return HIGH; }
bool bleHasLiveClient() { return connected; }
long map(long x, long inMin, long inMax, long outMin, long outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
void drawAdcRecovery() {}
void drawWeight(float) {}
void drawWeightAt(float, int) {}
void drawTime() {}
void drawBattery(unsigned long) {}
void drawButton() {}
void drawHeartBeat() {}
void drawTare() {}
void drawShutdownFail() {}
void drawAbout() {}
void drawDebug() {}
'''
    for signature in (
        "uint32_t mixEnergyDisplaySignature(uint32_t signature, uint32_t value)",
        "uint32_t mixEnergyDisplayText(uint32_t signature, const char *text)",
        "uint32_t energyDisplaySignature(unsigned long now)",
        "bool appTextScreenAvailable()",
        "void drawBle(unsigned long now)",
        "void updateOled()",
    ):
        source += signature + " {" + body(FIRMWARE, signature) + "}\n"
    source += r'''
void testPromptRedraw(bool redraw) {
  energyRuntime.oledFrames.invalidate();
  energyPolicy.settings.select(EnergyFeature::OledRedraw, redraw);
  b_ble_enabled = connected = true;
  b_wifiEnabled = false;
  promptState = {};
  appPromptScrollState = {};
  promptState.active = true;
  promptState.displayGeneration = 1;
  std::strcpy(promptState.text, "Gin");
  now = 100000;
  t_oled_refresh = now - i_oled_print_interval;
  updateOled();
  assert(u8g2.promptText == "Gin" && u8g2.promptX == 55);
  unsigned previousFrames = u8g2.frames;
  now += 100;
  std::strcpy(promptState.text, "Rum");
  ++promptState.displayGeneration;
  updateOled();
  assert(u8g2.frames == previousFrames + 1 && u8g2.promptText == "Rum");
  std::strcpy(promptState.text, "Carpano Antica Sweet Vermouth: 31g");
  ++promptState.displayGeneration;
  now += 100;
  updateOled();
  assert(u8g2.promptX == 0 && u8g2.promptText == promptState.text);
  previousFrames = u8g2.frames;
  now += 900;
  updateOled();
  assert(u8g2.frames == previousFrames + (redraw ? 0 : 1) && u8g2.promptX == 0);
  previousFrames = u8g2.frames;
  now += 200;
  updateOled();
  assert(u8g2.frames == previousFrames + 1 && u8g2.promptX == -1);
  b_menu = true;
  now += 100;
  updateOled();
  assert(u8g2.promptText.empty() && !appPromptScrollState.visible);
  b_menu = false;
  now += 100;
  updateOled();
  assert(u8g2.promptText == promptState.text && u8g2.promptX == 0);
  promptState.active = false;
  now += 100;
  updateOled();
  assert(u8g2.promptText.empty());
}

int main() {
  for (bool redraw : {false, true}) {
    for (unsigned cadence : {100, 200}) {
      for (unsigned phase = 0; phase < 2000; phase += 137) {
        energyRuntime.oledFrames.invalidate();
        energyPolicy.settings.select(EnergyFeature::OledRedraw, redraw);
        b_ble_enabled = true;
        connected = true;
        now = 10000 + phase;
        t_oled_refresh = now - cadence;
        updateOled();
        assert(u8g2.icon);
        const unsigned previousFrames = u8g2.frames;
        connected = false;
        now += cadence;
        updateOled();
        assert(u8g2.frames == previousFrames + 1);
        for (unsigned elapsed = 0; elapsed < 6000; elapsed += cadence) {
          assert(u8g2.icon == bool((now / 1000) & 1));
          now += cadence;
          updateOled();
        }
        connected = true;
        now += cadence;
        updateOled();
        assert(u8g2.icon);
        b_ble_enabled = false;
        now += cadence;
        updateOled();
        assert(!u8g2.icon && u8g2.bitmapCalls == 0);
      }
    }
  }
  for (bool redraw : {false, true}) testPromptRedraw(redraw);
}
'''
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        cpp, binary = path / "redraw.cpp", path / "redraw.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([
            compiler, "-std=c++17", "-include", "initializer_list",
            "-I" + str(ROOT / "include"), str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("BLE state and App Text content, scrolling, visibility render correctly with OLED Redraw on/off")


if __name__ == "__main__":
    main()
