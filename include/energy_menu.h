#ifndef ENERGY_MENU_H
#define ENERGY_MENU_H

void toggleEnergySerialQuiet();
void toggleEnergyOledRedraw();
void toggleEnergyOledIdle();
void toggleEnergyLightSleep();
void toggleEnergyUsbSleepTest();

char menuEnergySerialQuietLabel[] = "Serial Quiet o";
char menuEnergyOledRedrawLabel[] = "OLED Redraw o";
char menuEnergyOledIdleLabel[] = "OLED Idle o";
char menuEnergyLightSleepLabel[] = "Light Sleep o";
char menuEnergyUsbSleepTestLabel[] = "USB Sleep Test o";

const Menu menuEnergySerialQuiet = { menuEnergySerialQuietLabel, toggleEnergySerialQuiet, NULL, &menuPower };
const Menu menuEnergyOledRedraw = { menuEnergyOledRedrawLabel, toggleEnergyOledRedraw, NULL, &menuPower };
const Menu menuEnergyOledIdle = { menuEnergyOledIdleLabel, toggleEnergyOledIdle, NULL, &menuPower };
const Menu menuEnergyLightSleep = { menuEnergyLightSleepLabel, toggleEnergyLightSleep, NULL, &menuPower };
const Menu menuEnergyUsbSleepTest = { menuEnergyUsbSleepTestLabel, toggleEnergyUsbSleepTest, NULL, &menuPower };

char *energyFeatureRows[] = {
  menuEnergySerialQuietLabel,
  menuEnergyOledRedrawLabel,
  menuEnergyOledIdleLabel,
  menuEnergyLightSleepLabel,
  menuEnergyUsbSleepTestLabel,
};
static_assert(sizeof(energyFeatureRows) / sizeof(energyFeatureRows[0]) ==
              static_cast<size_t>(EnergyFeature::Count));

inline void updateEnergyMenuRow(EnergyFeature feature) {
  const uint8_t index = static_cast<uint8_t>(feature);
  if (index >= static_cast<uint8_t>(EnergyFeature::Count)) return;
  char *row = energyFeatureRows[index];
  row[strlen(row) - 1] = energyPolicy.settings.selected(feature) ? 'x' : 'o';
}

inline void refreshEnergyMenuRows() {
  for (uint8_t index = 0; index < static_cast<uint8_t>(EnergyFeature::Count); ++index) {
    updateEnergyMenuRow(static_cast<EnergyFeature>(index));
  }
}

inline void showEnergyAction(const char *label, bool enabled, bool stored) {
  actionMessage = stored ? String(label) : "Save Failed";
  actionMessage2 = stored ? (enabled ? "ON" : "OFF") : String(label);
  t_actionMessage = millis();
  t_actionMessageDelay = 1000;
  invalidateMenuFrame();
}

inline void toggleEnergyFeature(EnergyFeature feature, const char *label) {
  const bool wasEnabled = energyPolicy.settings.selected(feature);
  const bool enabled = !wasEnabled;
  const bool stored = energyStoreFeature(feature, enabled);
  if (stored) {
    energyPolicy.settings.select(feature, enabled);
    applyEnergyFeatureTransition(feature, wasEnabled, enabled);
    updateEnergyMenuRow(feature);
  }
  showEnergyAction(label, enabled, stored);
}

void toggleEnergySerialQuiet() { toggleEnergyFeature(EnergyFeature::SerialQuiet, "Serial Quiet"); }
void toggleEnergyOledRedraw() { toggleEnergyFeature(EnergyFeature::OledRedraw, "OLED Redraw"); }
void toggleEnergyOledIdle() { toggleEnergyFeature(EnergyFeature::OledIdle, "OLED Idle"); }
void toggleEnergyLightSleep() {
  const bool enabled = !energyPolicy.settings.selected(EnergyFeature::LightSleep);
  if (enabled) {
    const bool applied = setEnergyLightSleepEnabled(true);
    const bool stored = applied && energyStoreFeature(EnergyFeature::LightSleep, true);
    if (stored) {
      energyPolicy.settings.select(EnergyFeature::LightSleep, true);
    } else if (applied) {
      setEnergyLightSleepEnabled(false);
    }
    updateEnergyMenuRow(EnergyFeature::LightSleep);
    showEnergyAction("Light Sleep", true, stored);
    return;
  }
  const bool applied = setEnergyLightSleepEnabled(false);
  energyPolicy.settings.select(EnergyFeature::LightSleep, false);
  const bool stored = energyStoreFeature(EnergyFeature::LightSleep, false);
  updateEnergyMenuRow(EnergyFeature::LightSleep);
  showEnergyAction("Light Sleep", false, applied && stored);
}
void toggleEnergyUsbSleepTest() { toggleEnergyFeature(EnergyFeature::UsbSleepTest, "USB Sleep Test"); }

#endif
