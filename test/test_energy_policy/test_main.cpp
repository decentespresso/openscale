#include <unity.h>
#include "energy_policy.h"

void setUp() {}
void tearDown() {}

void testFeaturesDefaultOff() {
  EnergyPolicy policy;
  TEST_ASSERT_EQUAL_UINT32(0, policy.settings.features);
  TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(EnergyFeature::Count));
}

void testFeaturesAreIndependent() {
  EnergyPolicy policy;
  policy.settings.select(EnergyFeature::OledStatic, true);
  TEST_ASSERT_TRUE(policy.featureEnabled(EnergyFeature::OledStatic));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::OledRedraw));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::LightSleep));
  policy.settings.select(EnergyFeature::OledStatic, false);
  TEST_ASSERT_EQUAL_UINT32(0, policy.settings.features);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testFeaturesDefaultOff);
  RUN_TEST(testFeaturesAreIndependent);
  return UNITY_END();
}
