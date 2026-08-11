#ifndef ENERGY_MENU_H
#define ENERGY_MENU_H

void toggleEnergySerialQuiet();
void toggleEnergyPowerCadence();
void toggleEnergyOledRedraw();
void toggleEnergyOledIdle();
void toggleEnergyOledStatic();
void toggleEnergyMotionPoll();
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
void toggleEnergyAccRailOff();
#endif

Menu menuEnergy = { "Energy Saving", NULL, NULL, NULL };
Menu menuEnergyBack = { "Back", NULL, NULL, &menuEnergy };
char menuEnergySerialQuietLabel[] = "Serial Quiet o";
char menuEnergyPowerCadenceLabel[] = "Power Cadence o";
char menuEnergyOledRedrawLabel[] = "OLED Redraw o";
char menuEnergyOledIdleLabel[] = "OLED Idle o";
char menuEnergyOledStaticLabel[] = "OLED Static o";
char menuEnergyMotionPollLabel[] = "Motion Poll o";
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
char menuEnergyAccRailOffLabel[] = "ACC Rail Off o";
#endif

Menu menuEnergySerialQuiet = { menuEnergySerialQuietLabel, toggleEnergySerialQuiet, NULL, &menuEnergy };
Menu menuEnergyPowerCadence = { menuEnergyPowerCadenceLabel, toggleEnergyPowerCadence, NULL, &menuEnergy };
Menu menuEnergyOledRedraw = { menuEnergyOledRedrawLabel, toggleEnergyOledRedraw, NULL, &menuEnergy };
Menu menuEnergyOledIdle = { menuEnergyOledIdleLabel, toggleEnergyOledIdle, NULL, &menuEnergy };
Menu menuEnergyOledStatic = { menuEnergyOledStaticLabel, toggleEnergyOledStatic, NULL, &menuEnergy };
Menu menuEnergyMotionPoll = { menuEnergyMotionPollLabel, toggleEnergyMotionPoll, NULL, &menuEnergy };
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
Menu menuEnergyAccRailOff = { menuEnergyAccRailOffLabel, toggleEnergyAccRailOff, NULL, &menuEnergy };
#endif

Menu *energyMenu[] = {
  &menuEnergyBack,
  &menuEnergySerialQuiet,
  &menuEnergyPowerCadence,
  &menuEnergyOledRedraw,
  &menuEnergyOledIdle,
  &menuEnergyOledStatic,
  &menuEnergyMotionPoll,
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
  &menuEnergyAccRailOff,
#endif
};

char *energyFeatureRows[] = {
  menuEnergySerialQuietLabel,
  menuEnergyPowerCadenceLabel,
  menuEnergyOledRedrawLabel,
  menuEnergyOledIdleLabel,
  menuEnergyOledStaticLabel,
  menuEnergyMotionPollLabel,
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
  menuEnergyAccRailOffLabel,
#endif
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
void toggleEnergyMotionPoll() { toggleEnergyFeature(EnergyFeature::MotionPoll, "Motion Poll"); }
#if defined(ACC_PWR_CTRL) && defined(V8_1) && !defined(ACC_MPU6050) && !defined(ACC_BMA400)
void toggleEnergyAccRailOff() { toggleEnergyFeature(EnergyFeature::AccRailOff, "ACC Rail Off"); }
#endif

#endif
