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
  TEST_ASSERT_TRUE(gate.shouldRun(100, 1000));
  TEST_ASSERT_FALSE(gate.shouldRun(1099, 1000));
  TEST_ASSERT_TRUE(gate.shouldRun(1100, 1000));
  TEST_ASSERT_FALSE(gate.shouldRun(1101, 1000));

  CadenceGate rollover;
  TEST_ASSERT_TRUE(rollover.shouldRun(0xfffffff0u, 100));
  TEST_ASSERT_FALSE(rollover.shouldRun(0x53u, 100));
  TEST_ASSERT_TRUE(rollover.shouldRun(0x54u, 100));
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

void testBatteryDisplayUsesVisibleBuckets() {
  TEST_ASSERT_EQUAL_UINT8(0, EnergyRuntimePolicy::batteryDisplayBucket(5));
  TEST_ASSERT_EQUAL_UINT8(1, EnergyRuntimePolicy::batteryDisplayBucket(6));
  TEST_ASSERT_EQUAL_UINT8(1, EnergyRuntimePolicy::batteryDisplayBucket(25));
  TEST_ASSERT_EQUAL_UINT8(2, EnergyRuntimePolicy::batteryDisplayBucket(26));
  TEST_ASSERT_EQUAL_UINT8(3, EnergyRuntimePolicy::batteryDisplayBucket(75));
  TEST_ASSERT_EQUAL_UINT8(4, EnergyRuntimePolicy::batteryDisplayBucket(76));
}

void testIdleDeadlineUsesElapsedTimeAndHandlesRollover() {
  TEST_ASSERT_EQUAL_UINT32(75, EnergyRuntimePolicy::timeUntil(125, 100, 100));
  TEST_ASSERT_EQUAL_UINT32(0, EnergyRuntimePolicy::timeUntil(200, 100, 100));
  TEST_ASSERT_EQUAL_UINT32(50, EnergyRuntimePolicy::timeUntil(0x22u, 0xfffffff0u, 100));
}

void testIdleDeadlineSelectsEarlierWork() {
  TEST_ASSERT_EQUAL_UINT32(25, EnergyRuntimePolicy::earlier(100, 25));
  TEST_ASSERT_EQUAL_UINT32(25, EnergyRuntimePolicy::earlier(25, 100));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(testOledRedrawSkipsUnchangedFrames);
  RUN_TEST(testOledStaticImpliesGatingAndUsesThirtySeconds);
  RUN_TEST(testCadenceRunsExactlyAtDeadlineAndAcrossRollover);
  RUN_TEST(testBatteryConfirmationUsesDistinctSamples);
  RUN_TEST(testBatteryDisplayUsesVisibleBuckets);
  RUN_TEST(testIdleDeadlineUsesElapsedTimeAndHandlesRollover);
  RUN_TEST(testIdleDeadlineSelectsEarlierWork);
  return UNITY_END();
}
