#include <unity.h>
#include "energy_runtime_policy.h"

void setUp() {}
void tearDown() {}

void testOledRedrawSkipsUnchangedFrames() {
  OledFrameGate gate;
  TEST_ASSERT_TRUE(gate.shouldRender(true, 1, 100));
  TEST_ASSERT_FALSE(gate.shouldRender(true, 1, 200));
  TEST_ASSERT_TRUE(gate.shouldRender(true, 2, 201));
  TEST_ASSERT_TRUE(gate.shouldRender(false, 2, 202));
}

void testOledStaticImpliesGatingAndUsesThirtySeconds() {
  OledFrameGate gate;
  const bool oledRedraw = false;
  const bool oledStatic = true;
  const bool gatingEnabled = oledRedraw || oledStatic;
  TEST_ASSERT_TRUE(gatingEnabled);
  TEST_ASSERT_TRUE(gate.shouldRender(gatingEnabled, 7, 100, 30000));
  TEST_ASSERT_FALSE(gate.shouldRender(gatingEnabled, 7, 30099, 30000));
  TEST_ASSERT_TRUE(gate.shouldRender(gatingEnabled, 7, 30100, 30000));
}

void testCadenceRunsExactlyAtDeadlineAndAcrossRollover() {
  CadenceGate gate;
  TEST_ASSERT_TRUE(gate.shouldRun(true, 100, 1000));
  TEST_ASSERT_FALSE(gate.shouldRun(true, 1099, 1000));
  TEST_ASSERT_TRUE(gate.shouldRun(true, 1100, 1000));
  TEST_ASSERT_FALSE(gate.shouldRun(true, 1101, 1000));

  CadenceGate rollover;
  TEST_ASSERT_TRUE(rollover.shouldRun(true, 0xfffffff0u, 100));
  TEST_ASSERT_FALSE(rollover.shouldRun(true, 0x53u, 100));
  TEST_ASSERT_TRUE(rollover.shouldRun(true, 0x54u, 100));
}

void testCadenceDisableRestoresBaselineOnce() {
  CadenceGate gate;
  TEST_ASSERT_TRUE(gate.shouldRun(true, 10, 100));
  TEST_ASSERT_FALSE(gate.shouldRun(true, 11, 100));
  gate.reset();
  TEST_ASSERT_TRUE(gate.shouldRun(false, 12, 100));
  TEST_ASSERT_TRUE(gate.shouldRun(false, 13, 100));
  TEST_ASSERT_TRUE(gate.shouldRun(true, 14, 100));
}

void testMotionPollCachesForOneHundredMilliseconds() {
  MotionSampleGate gate;
  TEST_ASSERT_FALSE(gate.shouldRead(true, false, false, 0));
  TEST_ASSERT_TRUE(gate.shouldRead(true, false, true, 0xfffffff0u));
  TEST_ASSERT_FALSE(gate.shouldRead(true, false, true, 20));
  TEST_ASSERT_TRUE(gate.shouldRead(true, false, true, 100));
}

void testExplicitFreshMotionReadBypassesCache() {
  MotionSampleGate gate;
  TEST_ASSERT_TRUE(gate.shouldRead(true, false, true, 100));
  TEST_ASSERT_FALSE(gate.shouldRead(true, false, true, 101));
  TEST_ASSERT_TRUE(gate.shouldRead(true, true, true, 101));
  TEST_ASSERT_TRUE(gate.shouldRead(false, false, true, 102));
}

void testOledIdleTransitionsAndExplicitOffPrecedence() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayIdleMode::Active),
                          static_cast<uint8_t>(EnergyRuntimePolicy::displayMode(true, false, false, 29999)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayIdleMode::Dimmed),
                          static_cast<uint8_t>(EnergyRuntimePolicy::displayMode(true, false, false, 30000)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayIdleMode::Off),
                          static_cast<uint8_t>(EnergyRuntimePolicy::displayMode(true, false, false, 120000)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayIdleMode::Active),
                          static_cast<uint8_t>(EnergyRuntimePolicy::displayMode(false, false, false, 120000)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DisplayIdleMode::Off),
                          static_cast<uint8_t>(EnergyRuntimePolicy::displayMode(false, true, true, 0)));
}

void testBatteryConfirmationUsesDistinctSamples() {
  BatterySampleGate samples;
  TEST_ASSERT_FALSE(samples.shouldEvaluate(0));
  TEST_ASSERT_TRUE(samples.shouldEvaluate(1));
  TEST_ASSERT_FALSE(samples.shouldEvaluate(1));
  TEST_ASSERT_TRUE(samples.shouldEvaluate(2));
  TEST_ASSERT_FALSE(EnergyRuntimePolicy::lowBatteryConfirmed(1));
  TEST_ASSERT_TRUE(EnergyRuntimePolicy::lowBatteryConfirmed(2));
}

void testSharedDispatchSkipsEmptyAndUnrelatedMasks() {
  const uint32_t oledIdleBit = 1u << 3;
  TEST_ASSERT_FALSE(EnergyRuntimePolicy::shouldServiceFeature(0, oledIdleBit));
  TEST_ASSERT_FALSE(EnergyRuntimePolicy::shouldServiceFeature(1u << 0, oledIdleBit));
  TEST_ASSERT_FALSE(EnergyRuntimePolicy::shouldServiceFeature(1u << 1, oledIdleBit));
  TEST_ASSERT_TRUE(EnergyRuntimePolicy::shouldServiceFeature(oledIdleBit, oledIdleBit));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testOledRedrawSkipsUnchangedFrames);
  RUN_TEST(testOledStaticImpliesGatingAndUsesThirtySeconds);
  RUN_TEST(testCadenceRunsExactlyAtDeadlineAndAcrossRollover);
  RUN_TEST(testCadenceDisableRestoresBaselineOnce);
  RUN_TEST(testMotionPollCachesForOneHundredMilliseconds);
  RUN_TEST(testExplicitFreshMotionReadBypassesCache);
  RUN_TEST(testOledIdleTransitionsAndExplicitOffPrecedence);
  RUN_TEST(testBatteryConfirmationUsesDistinctSamples);
  RUN_TEST(testSharedDispatchSkipsEmptyAndUnrelatedMasks);
  return UNITY_END();
}
