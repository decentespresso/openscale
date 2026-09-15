#ifndef ENERGY_MENU_H
#define ENERGY_MENU_H

void toggleEnergyOledRedraw();
void toggleEnergyOledIdle();
void toggleEnergyLightSleep();
void toggleEnergyUsbSleepTest();

char menuEnergyOledRedrawLabel[] = "OLED Redraw o";
char menuEnergyOledIdleLabel[] = "OLED Idle o";
char menuEnergyLightSleepLabel[] = "Light Sleep o";
char menuEnergyUsbSleepTestLabel[] = "USB Sleep Test o";

const Menu menuEnergyOledRedraw = { menuEnergyOledRedrawLabel, toggleEnergyOledRedraw, NULL, &menuPower };
const Menu menuEnergyOledIdle = { menuEnergyOledIdleLabel, toggleEnergyOledIdle, NULL, &menuPower };
const Menu menuEnergyLightSleep = { menuEnergyLightSleepLabel, toggleEnergyLightSleep, NULL, &menuPower };
const Menu menuEnergyUsbSleepTest = { menuEnergyUsbSleepTestLabel, toggleEnergyUsbSleepTest, NULL, &menuPower };

char *energyFeatureRows[] = {
  menuEnergyOledRedrawLabel,
  menuEnergyOledIdleLabel,
  menuEnergyLightSleepLabel,
  menuEnergyLightSleepLabel,
  menuEnergyUsbSleepTestLabel,
};
static_assert(sizeof(energyFeatureRows) / sizeof(energyFeatureRows[0]) ==
              static_cast<size_t>(EnergyFeature::Count));

inline void updateEnergyMenuRow(EnergyFeature feature) {
  const uint8_t index = static_cast<uint8_t>(feature);
  if (index >= static_cast<uint8_t>(EnergyFeature::Count)) return;
  char *row = energyFeatureRows[index];
  if (feature == EnergyFeature::LightSleep ||
      feature == EnergyFeature::LightSleepPlus) {
    const bool enabled = energyPolicy.settings.selected(EnergyFeature::LightSleep);
    const bool plus = energyPolicy.settings.lightSleepPlusActive();
    row[strlen(row) - 1] = !enabled ? 'o' : plus ? '+' : 'x';
    return;
  }
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

inline bool storeEnergyLightSleepProfile(bool enabled, bool plus,
                                         bool oldEnabled, bool oldPlus) {
  if (!energyStoreFeature(EnergyFeature::LightSleep, enabled)) return false;
  if (energyStoreFeature(EnergyFeature::LightSleepPlus, enabled && plus)) return true;
  energyStoreFeature(EnergyFeature::LightSleep, oldEnabled);
  energyStoreFeature(EnergyFeature::LightSleepPlus, oldPlus);
  return false;
}

void toggleEnergyOledRedraw() { toggleEnergyFeature(EnergyFeature::OledRedraw, "OLED Redraw"); }
void toggleEnergyOledIdle() { toggleEnergyFeature(EnergyFeature::OledIdle, "OLED Idle"); }
void toggleEnergyLightSleep() {
  const bool wasEnabled = energyPolicy.settings.selected(EnergyFeature::LightSleep);
  const bool wasPlus = energyPolicy.settings.lightSleepPlusActive();
  if (!wasEnabled) {
    const bool applied = setEnergyLightSleepEnabled(true);
    const bool stored = applied &&
      storeEnergyLightSleepProfile(true, false, false, false);
    if (stored) {
      energyPolicy.settings.select(EnergyFeature::LightSleep, true);
      energyPolicy.settings.select(EnergyFeature::LightSleepPlus, false);
    } else if (applied) {
      setEnergyLightSleepEnabled(false);
    }
    updateEnergyMenuRow(EnergyFeature::LightSleep);
    showEnergyAction("Light Sleep", true, stored);
    return;
  }
  if (!wasPlus) {
    const bool stored = storeEnergyLightSleepProfile(true, true, true, false);
    if (stored) {
      energyPolicy.settings.select(EnergyFeature::LightSleepPlus, true);
    }
    updateEnergyMenuRow(EnergyFeature::LightSleep);
    showEnergyAction("Light Sleep+", true, stored);
    return;
  }
  const bool applied = setEnergyLightSleepEnabled(false);
  energyPolicy.settings.select(EnergyFeature::LightSleep, false);
  energyPolicy.settings.select(EnergyFeature::LightSleepPlus, false);
  const bool stored = storeEnergyLightSleepProfile(false, false, true, true);
  updateEnergyMenuRow(EnergyFeature::LightSleep);
  showEnergyAction("Light Sleep", false, applied && stored);
}
void toggleEnergyUsbSleepTest() { toggleEnergyFeature(EnergyFeature::UsbSleepTest, "USB Sleep Test"); }

#endif
