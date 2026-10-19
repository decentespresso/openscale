import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_energy_stage0_contract import body, FIRMWARE, STORAGE, ROOT


def function(source, signature):
    return signature + " {" + body(source, signature) + "}\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--acebutton", type=Path, default=ROOT / ".pio.nosync/libdeps/esp32s3-energy-menu/AceButton")
    args = parser.parse_args()
    library = args.acebutton.resolve()
    assert "version=1.10.1" in (library / "library.properties").read_text()
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    setup = body(FIRMWARE, "void setup()")
    assert setup.index("button_init()") < setup.index("primeScaleButtonInputs()") < setup.index("setEnergyLightSleepEnabled(true)")
    loop = body(FIRMWARE, "void loop()")
    assert loop.index("serviceScaleButtonInputs(true)") < loop.index("finishEnergyButtonWakeDebounce()") < loop.index("processWsPendingCmds()") < loop.index("processBleStatusResponse()")
    source = r'''
#include <cassert>
#include <map>
#include <string>
#include "ace_button/AceButton.h"
#include "energy_policy.h"
#include "timing.h"
using namespace ace_button;
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2;
constexpr unsigned long BUTTON_POLL_INTERVAL_MS = 2, DOUBLECLICK_DELAY = 600;
constexpr unsigned long CLICK_DELAY = 180, LONGPRESS_DELAY = 900;
constexpr unsigned long ENERGY_BUTTON_RESPONSE_BOOST_MS = 50;
unsigned long now = 0, releaseAt = 0;
int pins[] = {HIGH, HIGH, HIGH};
int presses[3] = {}, releases[3] = {}, doubles[3] = {}, longs[3] = {};
bool b_ota = false, b_buttonChordSuppressUntilRelease = false;
struct {
  bool buttonGestureActive = false, lightSleepButtonTracePending = false;
  unsigned long lastButtonActivityAt = 0, lastButtonPoll = 0, lastLightSleepButtonPollAt = 0;
} energyIdle;
EnergyPolicy energyPolicy;
ButtonConfig config1;
AceButton buttonCircle(&config1), buttonSquare(&config1);
unsigned long millis() { return now; }
void delay(unsigned long ms) {
  now += ms;
  if (releaseAt && now >= releaseAt) pins[1] = pins[2] = HIGH;
}
int digitalRead(uint8_t pin) { return pins[pin]; }
void pinMode(int, int) {}
void aceButtonHandleEvent(AceButton *button, uint8_t event, uint8_t) {
  int pin = button->getPin();
  if (event == AceButton::kEventPressed) presses[pin]++;
  if (event == AceButton::kEventReleased) releases[pin]++;
  if (event == AceButton::kEventDoubleClicked) doubles[pin]++;
  if (event == AceButton::kEventLongPressed) longs[pin]++;
}
'''
    for signature in (
        "bool anyScaleButtonPressed()",
        "bool buttonChecksSuppressedUntilRelease()",
        "bool serviceEnergyButtonGesture(unsigned long now)",
        "bool energyResponsiveButtonBoostActive(unsigned long now)",
        "bool serviceScaleButtonInputs(bool forcePoll)",
        "void primeScaleButtonInputs()",
        "void finishEnergyButtonWakeDebounce()",
        "void button_init()",
    ):
        source += function(FIRMWARE, signature)
    source += r'''
void boot(bool plus, int held = 0) {
  now = 10000;
  releaseAt = 0;
  pins[1] = held == 1 ? LOW : HIGH;
  pins[2] = held == 2 ? LOW : HIGH;
  for (int pin : {1, 2}) presses[pin] = releases[pin] = doubles[pin] = longs[pin] = 0;
  b_ota = b_buttonChordSuppressUntilRelease = false;
  energyIdle = {};
  energyPolicy.settings.features = 0;
  energyPolicy.settings.select(EnergyFeature::LightSleep, true);
  energyPolicy.settings.select(EnergyFeature::LightSleepPlus, plus);
  config1.setDebounceDelay(20);
  button_init();
  primeScaleButtonInputs();
  assert(presses[1] == 0 && presses[2] == 0);
}
void wake() {
  if (serviceScaleButtonInputs(true)) finishEnergyButtonWakeDebounce();
}
void settle() {
  serviceScaleButtonInputs(true);
  delay(config1.getDebounceDelay());
  serviceScaleButtonInputs(true);
}
void testButtons() {
  for (bool plus : {false, true}) {
    boot(plus);
    assert(!energyResponsiveButtonBoostActive(now));
    pins[1] = LOW;
    serviceEnergyButtonGesture(now);
    assert(energyResponsiveButtonBoostActive(now));
    delay(1000);
    serviceEnergyButtonGesture(now);
    assert(energyResponsiveButtonBoostActive(now));
    pins[1] = HIGH;
    delay(50);
    serviceEnergyButtonGesture(now);
    assert(energyResponsiveButtonBoostActive(now));
    delay(1);
    assert(!energyResponsiveButtonBoostActive(now));
    pins[1] = LOW;
    serviceEnergyButtonGesture(now);
    energyPolicy.settings.select(EnergyFeature::LightSleep, false);
    assert(!energyResponsiveButtonBoostActive(now));
  }
  for (int pin : {1, 2}) {
    boot(false);
    assert(buttonCircle.getLastButtonState() == HIGH && buttonSquare.getLastButtonState() == HIGH);
    pins[pin] = LOW;
    const auto started = now;
    wake();
    assert(presses[pin] == 1 && now - started == config1.getDebounceDelay());
    wake();
    assert(presses[pin] == 1);
    pins[pin] = HIGH;
    settle();
    assert(releases[pin] == 1);
    pins[pin] = LOW;
    wake();
    pins[pin] = HIGH;
    settle();
    assert(presses[pin] == 2 && doubles[pin] == 1);
    delay(DOUBLECLICK_DELAY + 1);
    pins[pin] = LOW;
    wake();
    delay(LONGPRESS_DELAY);
    serviceScaleButtonInputs(true);
    assert(longs[pin] == 1);

    boot(true);
    pins[pin] = LOW;
    const auto aggressiveStarted = now;
    wake();
    assert(presses[pin] == 0 && now == aggressiveStarted);
    delay(config1.getDebounceDelay());
    serviceScaleButtonInputs(true);
    assert(presses[pin] == 1);

    boot(false, pin);
    assert(energyIdle.buttonGestureActive);
    pins[pin] = HIGH;
    settle();
    pins[pin] = LOW;
    wake();
    assert(presses[pin] == 1);
  }
  boot(false);
  pins[1] = pins[2] = LOW;
  wake();
  assert(presses[1] == 1 && presses[2] == 1);
  boot(false);
  pins[1] = LOW;
  releaseAt = now + 4;
  wake();
  assert(presses[1] == 0);
  for (bool ota : {false, true}) {
    boot(false);
    pins[1] = LOW;
    b_ota = ota;
    b_buttonChordSuppressUntilRelease = !ota;
    const auto before = now;
    wake();
    assert(presses[1] == 0 && now == before);
  }
  boot(false);
  now = 65530;
  pins[1] = LOW;
  wake();
  assert(presses[1] == 1);
  boot(false);
  config1.setDebounceDelay(31);
  pins[1] = LOW;
  const auto configuredStart = now;
  wake();
  assert(presses[1] == 1 && now - configuredStart == 32);
}
enum { PT_U8, PT_U16, PT_INVALID };
struct Preferences {
  std::map<std::string, std::pair<int, unsigned>> values;
  bool failPlus = false;
  int getType(const char *key) { return isKey(key) ? values.at(key).first : PT_INVALID; }
  bool isKey(const char *key) { return values.count(key); }
  unsigned getUChar(const char *key, unsigned fallback) { return getType(key) == PT_U8 ? values.at(key).second : fallback; }
  unsigned getUShort(const char *key, unsigned fallback) { return getType(key) == PT_U16 ? values.at(key).second : fallback; }
  size_t putBool(const char *key, bool value) {
    if (failPlus && std::string(key) == "e_light_plus") return 0;
    values[key] = {PT_U8, value};
    return sizeof(bool);
  }
  size_t putUShort(const char *key, uint16_t value) { values[key] = {PT_U16, value}; return sizeof(uint16_t); }
  bool remove(const char *key) { return values.erase(key); }
} settingsPreferences;
'''
    source += STORAGE.split("#if HDS_ENABLE_ENERGY_MENU", 1)[1].split("#endif", 1)[0]
    for signature in (
        "inline bool storagePutBool(const char *key, bool value)",
        "inline bool storageLoadValidatedBool(const char *key, bool defaultValue, bool &value)",
        "inline bool storageRemoveIfPresent(const char *key)",
        "inline bool energyLoadSettings(EnergySettings &settings)",
    ):
        source += function(STORAGE, signature)
    source += r'''
void testMigration() {
  for (uint16_t schema = 0; schema <= 11; schema++) {
    for (int light : {-1, 0, 1, 2}) {
      for (int plus : {-1, 0, 1}) {
        settingsPreferences = {};
        if (schema) settingsPreferences.putUShort(KEY_ENERGY_SCHEMA, schema);
        if (light >= 0) settingsPreferences.values["e_light_sleep"] = {PT_U8, unsigned(light)};
        if (plus >= 0) settingsPreferences.putBool("e_light_plus", plus);
        settingsPreferences.putBool("e_oled_idle", true);
        EnergySettings settings;
        assert(energyLoadSettings(settings));
        const bool expected = light == 1 && (plus == 1 || (plus == -1 && schema >= 5 && schema <= 9));
        assert(settings.enabled(EnergyFeature::LightSleep) == (light == 1));
        assert(settings.lightSleepPlusActive() == expected);
        assert(settings.enabled(EnergyFeature::OledIdle));
        assert(energyLoadSettings(settings) && settings.lightSleepPlusActive() == expected);
      }
    }
  }
  for (uint16_t schema = 5; schema <= 9; schema++) {
    settingsPreferences = {};
    settingsPreferences.putUShort(KEY_ENERGY_SCHEMA, schema);
    settingsPreferences.putBool("e_light_sleep", true);
    settingsPreferences.failPlus = true;
    EnergySettings settings;
    assert(!energyLoadSettings(settings));
    assert(settingsPreferences.getUShort(KEY_ENERGY_SCHEMA, 0) == schema);
    settingsPreferences.failPlus = false;
    assert(energyLoadSettings(settings) && settings.lightSleepPlusActive());
  }
  for (const char *key : {KEY_ENERGY_SCHEMA, "e_light_sleep", "e_light_plus"}) {
    settingsPreferences = {};
    settingsPreferences.putUShort(KEY_ENERGY_SCHEMA, 8);
    settingsPreferences.putBool("e_light_sleep", true);
    settingsPreferences.values[key] = {PT_INVALID, 1};
    EnergySettings settings;
    assert(energyLoadSettings(settings) && !settings.lightSleepPlusActive());
  }
}
int main() { testButtons(); testMigration(); }
'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "Arduino.h").write_text(
            "#pragma once\n#include <cstdint>\n#include <cstddef>\n"
            "#define HIGH 1\n#define LOW 0\n#define INPUT_PULLUP 2\n#define PROGMEM\n"
            "#define pgm_read_ptr(p) (*(p))\nclass __FlashStringHelper;\n"
            "unsigned long millis();\nint digitalRead(uint8_t);\n", encoding="utf-8")
        cpp, binary = path / "regression.cpp", path / "regression.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([
            compiler, "-std=c++17", "-DHDS_ENABLE_ENERGY_MENU=1",
            "-I" + str(path), "-I" + str(ROOT / "include"), "-I" + str(library / "src"),
            str(cpp), str(library / "src/ace_button/AceButton.cpp"),
            str(library / "src/ace_button/ButtonConfig.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("Energy button and schema migration runtime regressions passed (AceButton 1.10.1)")


if __name__ == "__main__":
    main()
