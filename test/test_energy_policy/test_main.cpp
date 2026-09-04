#include <unity.h>
#include "energy_policy.h"

void setUp() {}
void tearDown() {}

void testFeaturesDefaultOff() {
  EnergyPolicy policy;
  TEST_ASSERT_EQUAL_UINT32(0, policy.settings.features);
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(EnergyFeature::Count));
}

void testUsbSleepPolicyCombinations() {
  EnergySettings settings;
  TEST_ASSERT_FALSE(settings.lightSleepAllowed(true));
  TEST_ASSERT_FALSE(settings.usbSleepTestActive());
  settings.select(EnergyFeature::UsbSleepTest, true);
  TEST_ASSERT_FALSE(settings.lightSleepAllowed(true));
  TEST_ASSERT_FALSE(settings.usbSleepTestActive());
  settings.select(EnergyFeature::UsbSleepTest, false);
  settings.select(EnergyFeature::LightSleep, true);
  TEST_ASSERT_FALSE(settings.lightSleepAllowed(true));
  TEST_ASSERT_FALSE(settings.usbSleepTestActive());
  settings.select(EnergyFeature::UsbSleepTest, true);
  TEST_ASSERT_TRUE(settings.lightSleepAllowed(true));
  TEST_ASSERT_TRUE(settings.usbSleepTestActive());
}

void testFeaturesAreIndependent() {
  EnergyPolicy policy;
  policy.settings.select(EnergyFeature::OledIdle, true);
  TEST_ASSERT_TRUE(policy.featureEnabled(EnergyFeature::OledIdle));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::OledRedraw));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::LightSleep));
  policy.settings.select(EnergyFeature::OledIdle, false);
  TEST_ASSERT_EQUAL_UINT32(0, policy.settings.features);
}

void testActivityTimingHandlesRollover() {
  EnergyPolicy policy;
  policy.begin(0xfffffff0u);
  policy.recordActivity(0xfffffff8u);
  TEST_ASSERT_EQUAL_UINT32(12, policy.inactiveFor(4u));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testFeaturesDefaultOff);
  RUN_TEST(testFeaturesAreIndependent);
  RUN_TEST(testUsbSleepPolicyCombinations);
  RUN_TEST(testActivityTimingHandlesRollover);
  return UNITY_END();
}
