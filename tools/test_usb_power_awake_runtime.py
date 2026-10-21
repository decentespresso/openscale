import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#define ESP32
#define HDS_ENABLE_ENERGY_MENU 0
#define HDS_ENABLE_GRINDER 0
#define HDS_FEATURE_WIFI 0
#define HDS_FEATURE_WEBSERVER 0
#define HDS_FEATURE_WEBSOCKET 1
#ifdef TEST_USB_DETECT
#define USB_DET 8
#endif
#define BUTTON_PIN_BITMASK(pin) (1ULL << (pin))
@PIN_MASK@
constexpr int LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2, BATTERY_CHARGING = 10;
constexpr int OLED_SDIN = 7, OLED_SCLK = 15, OLED_DC = 16, OLED_RST = 17, OLED_CS = 18;
constexpr int SCALE_SCLK = 12, SCALE_PDWN = 13, SCALE_DOUT = 11;
constexpr int SCALE2_SCLK = 48, SCALE2_PDWN = 9, SCALE2_DOUT = 47;
constexpr int I2C_SDA = 5, I2C_SCL = 4, ACC_PWR_CTRL = 14, PWR_CTRL = 3;
constexpr int RTC_GPIO_MODE_INPUT_ONLY = 0, ESP_EXT1_WAKEUP_ANY_LOW = 0;
constexpr int U8G2_R0 = 0, U8G2_R2 = 2, FONT_M = 0;
enum esp_sleep_wakeup_cause_t {
  ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_EXT0, ESP_SLEEP_WAKEUP_EXT1,
  ESP_SLEEP_WAKEUP_TIMER, ESP_SLEEP_WAKEUP_TOUCHPAD, ESP_SLEEP_WAKEUP_ULP
};
using gpio_num_t = int;
bool b_powerOff = false, b_screenFlipped = false;
int effects = 0, i_wakeupPin = -1, GPIO_power_on_with = -1, i_wow_interval = 0;
unsigned long t_batteryRefresh = 1;
uint64_t GPIO_reason = 0, wakeStatus = 0, armedMask = 0;
esp_sleep_wakeup_cause_t wakeCause = ESP_SLEEP_WAKEUP_EXT1;
std::array<int, 64> levels;
std::vector<int> configured, released;
int digitalRead(int pin) { return levels[pin]; }
int rtc_gpio_get_level(int pin) { return levels[pin]; }
void rtc_gpio_hold_dis(int) {}
void rtc_gpio_init(int pin) { configured.push_back(pin); }
void rtc_gpio_set_direction(int, int) {}
void rtc_gpio_pullup_en(int) {}
void rtc_gpio_pulldown_dis(int) {}
void rtc_gpio_deinit(int pin) { released.push_back(pin); }
void gpio_hold_en(int) { ++effects; }
void gpio_deep_sleep_hold_en() { ++effects; }
void pinMode(int, int) { ++effects; }
void digitalWrite(int, int) { ++effects; }
void delay(int) { ++effects; }
void bleShutdown() { ++effects; }
void sendBlePowerOff(int) { ++effects; }
void sendWebsocketPowerOff(int) { ++effects; }
void wowCaptureBaselineForSleep() { ++effects; }
void wowArmSleepTimer() { ++effects; }
void fuelGaugeSleep() { ++effects; }
bool fuelGaugeLegacyHardware() { return false; }
void refreshOLED(char*, int) { ++effects; }
std::string String(int value) { return std::to_string(value); }
struct {
  template<class T> void print(T) { ++effects; }
  template<class T> void println(T) { ++effects; }
  template<class... T> void printf(const char*, T...) { ++effects; }
  void flush() { ++effects; }
} Serial;
struct {
  void setPowerSave(int) { ++effects; }
  void setFontDirection(int) { ++effects; }
  void setDisplayRotation(int) { ++effects; }
} u8g2;
struct { void powerDown() { ++effects; } } scale;
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return wakeCause; }
uint64_t esp_sleep_get_ext1_wakeup_status() { return wakeStatus; }
void esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int) { armedMask = mask; }
struct Sleep {};
void esp_deep_sleep_start() { throw Sleep{}; }
@FUNCTIONS@

int main() {
  const auto lowBatteryShutdown = []() { shut_down_low_battery(3.0f); };
  for (const auto shutdown : {esp32_sleep, shut_down_now, shut_down_now_nobeep,
                              shut_down_now_accidentTouch, +lowBatteryShutdown}) {
    for (int usb : {LOW, HIGH}) {
      levels.fill(HIGH);
      levels[8] = usb;
      effects = 0;
      b_powerOff = true;
      bool slept = false;
      try { shutdown(); } catch (const Sleep&) { slept = true; }
#ifdef USB_DET
      if (usb == LOW) {
        assert(!slept && !b_powerOff && effects == 0);
        continue;
      }
#endif
      assert(slept && effects > 0 && armedMask == deepSleepWakeMask());
    }
  }
  configured.clear();
  released.clear();
  configureWakePinsForDeepSleep();
  releaseWakePinsFromRtcMode();
  assert(configured == released);
  assert(configured == std::vector<int>({BUTTON_CIRCLE, BUTTON_SQUARE, BATTERY_CHARGING
#ifdef USB_DET
      , USB_DET
#endif
  }));
  assert(deepSleepWakeMask() == (PIN_BITMASK
#ifdef USB_DET
      | BUTTON_PIN_BITMASK(USB_DET)
#endif
  ));
  for (uint64_t mask = 1; mask < 16; ++mask) {
    wakeStatus = ((mask & 1) ? BUTTON_PIN_BITMASK(BUTTON_SQUARE) : 0) |
                 ((mask & 2) ? BUTTON_PIN_BITMASK(BUTTON_CIRCLE) : 0) |
                 ((mask & 4) ? BUTTON_PIN_BITMASK(BATTERY_CHARGING) : 0);
#ifdef USB_DET
    wakeStatus |= (mask & 8) ? BUTTON_PIN_BITMASK(USB_DET) : 0;
#endif
    if (!wakeStatus) continue;
    GPIO_power_on_with = -1;
    print_wakeup_reason();
    const int expected = mask & 1 ? BUTTON_SQUARE : mask & 2 ? BUTTON_CIRCLE : BATTERY_CHARGING;
    assert(GPIO_power_on_with == expected);
  }
}
'''


def main():
    power = (ROOT / "include/power.h").read_text(encoding="utf-8")
    signatures = [
        "bool usbPowerKeepsAwake()", "bool cancelUsbPoweredShutdown()",
        "void configureWakePinForDeepSleep(gpio_num_t pin)",
        "void configureWakePinsForDeepSleep()", "void releaseWakePinsFromRtcMode()",
        "uint64_t deepSleepWakeMask()", "void print_wakeup_reason()",
        "void esp32_sleep()", "void shut_down_now()",
        "void shut_down_low_battery(float voltage)", "void shut_down_now_nobeep()",
        "void shut_down_now_accidentTouch()",
    ]
    functions = "\n".join(f"{signature} {{ {block_after(power, signature)} }}" for signature in signatures)
    source = HARNESS.replace("@FUNCTIONS@", functions).replace(
        "@PIN_MASK@", re.search(r"^#define PIN_BITMASK .*", power, re.MULTILINE)[0]
    )
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="usb-awake-") as directory:
        cpp = Path(directory) / "runtime.cpp"
        binary = Path(directory) / ("runtime.exe" if os.name == "nt" else "runtime")
        cpp.write_text(source, encoding="utf-8")
        for flags in ([], ["-DTEST_USB_DETECT"]):
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
    print("USB awake shutdown guards, wake mask, and button priority checks passed")


if __name__ == "__main__":
    main()
