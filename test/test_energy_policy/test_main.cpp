#include <unity.h>
#include "energy_policy.h"

void setUp() {}
void tearDown() {}

void testFeaturesDefaultOff() {
  EnergyPolicy policy;
  TEST_ASSERT_FALSE(policy.anyFeatureEnabled());
  TEST_ASSERT_EQUAL_UINT32(0, policy.settings.features);
  TEST_ASSERT_EQUAL_UINT8(6, static_cast<uint8_t>(EnergyFeature::Count));
}

void testFeaturesAreIndependent() {
  EnergyPolicy policy;
  policy.settings.select(EnergyFeature::OledStatic, true);
  TEST_ASSERT_TRUE(policy.featureEnabled(EnergyFeature::OledStatic));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::OledRedraw));
  TEST_ASSERT_FALSE(policy.featureEnabled(EnergyFeature::OledIdle));
  policy.settings.select(EnergyFeature::OledStatic, false);
  TEST_ASSERT_FALSE(policy.anyFeatureEnabled());
}

void testActivityTimingHandlesRollover() {
  TEST_ASSERT_FALSE(EnergyPolicy::deadlineReached(0xfffffff5u, 0xfffffff0u, 10));
  TEST_ASSERT_TRUE(EnergyPolicy::deadlineReached(4u, 0xfffffff0u, 20));
  EnergyPolicy policy;
  policy.begin(0xfffffff0u);
  policy.recordActivity(0xfffffff8u);
  TEST_ASSERT_EQUAL_UINT32(12, policy.inactiveFor(4u));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testFeaturesDefaultOff);
  RUN_TEST(testFeaturesAreIndependent);
  RUN_TEST(testActivityTimingHandlesRollover);
  return UNITY_END();
}
