#ifndef ENERGY_MENU_H
#define ENERGY_MENU_H

void toggleEnergySerialQuiet();
void toggleEnergyPowerCadence();
void toggleEnergyOledRedraw();
void toggleEnergyOledIdle();
void toggleEnergyOledStatic();
void toggleEnergyLightSleep();

extern Menu menuEnergy;
Menu menuEnergyBack = { "Back", NULL, NULL, &menuEnergy };
Menu menuEnergy = { "Energy Saving", NULL, &menuEnergyBack, NULL };
char menuEnergySerialQuietLabel[] = "Serial Quiet o";
char menuEnergyPowerCadenceLabel[] = "Power Cadence o";
char menuEnergyOledRedrawLabel[] = "OLED Redraw o";
char menuEnergyOledIdleLabel[] = "OLED Idle o";
char menuEnergyOledStaticLabel[] = "OLED Static o";
char menuEnergyLightSleepLabel[] = "Light Sleep o";

const Menu menuEnergySerialQuiet = { menuEnergySerialQuietLabel, toggleEnergySerialQuiet, NULL, &menuEnergy };
const Menu menuEnergyPowerCadence = { menuEnergyPowerCadenceLabel, toggleEnergyPowerCadence, NULL, &menuEnergy };
const Menu menuEnergyOledRedraw = { menuEnergyOledRedrawLabel, toggleEnergyOledRedraw, NULL, &menuEnergy };
const Menu menuEnergyOledIdle = { menuEnergyOledIdleLabel, toggleEnergyOledIdle, NULL, &menuEnergy };
const Menu menuEnergyOledStatic = { menuEnergyOledStaticLabel, toggleEnergyOledStatic, NULL, &menuEnergy };
const Menu menuEnergyLightSleep = { menuEnergyLightSleepLabel, toggleEnergyLightSleep, NULL, &menuEnergy };

const Menu *const energyMenu[] = {
  &menuEnergyBack,
  &menuEnergySerialQuiet,
  &menuEnergyPowerCadence,
  &menuEnergyOledRedraw,
  &menuEnergyOledIdle,
  &menuEnergyOledStatic,
  &menuEnergyLightSleep,
};

char *energyFeatureRows[] = {
  menuEnergySerialQuietLabel,
  menuEnergyPowerCadenceLabel,
  menuEnergyOledRedrawLabel,
  menuEnergyOledIdleLabel,
  menuEnergyOledStaticLabel,
  menuEnergyLightSleepLabel,
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
void toggleEnergyPowerCadence() { toggleEnergyFeature(EnergyFeature::PowerCadence, "Power Cadence"); }
void toggleEnergyOledRedraw() { toggleEnergyFeature(EnergyFeature::OledRedraw, "OLED Redraw"); }
void toggleEnergyOledIdle() { toggleEnergyFeature(EnergyFeature::OledIdle, "OLED Idle"); }
void toggleEnergyOledStatic() { toggleEnergyFeature(EnergyFeature::OledStatic, "OLED Static"); }
void toggleEnergyLightSleep() { toggleEnergyFeature(EnergyFeature::LightSleep, "Light Sleep"); }

#endif
