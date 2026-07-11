#include <unity.h>
#include "pull_ota_version.h"

void setUp() {}
void tearDown() {}

void testStableVersionsStayStrict() {
  TEST_ASSERT_TRUE(pullOtaVersionIsStable("v3.1.14"));
  TEST_ASSERT_FALSE(pullOtaVersionIsStable("3.1.14-dev"));
  TEST_ASSERT_FALSE(pullOtaVersionIsStable("3.1"));
  TEST_ASSERT_FALSE(pullOtaVersionIsStable("3.1.14.1"));
}

void testDevBuildUsesNumericPrefix() {
  TEST_ASSERT_EQUAL_INT(0, pullOtaCompareVersionPrefixes("3.1.13", "3.1.13-dev"));
  TEST_ASSERT_EQUAL_INT(1, pullOtaCompareVersionPrefixes("3.1.14", "3.1.13-dev"));
  TEST_ASSERT_EQUAL_INT(-1, pullOtaCompareVersionPrefixes("3.1.12", "3.1.13-dev"));
  TEST_ASSERT_EQUAL_INT(0, pullOtaCompareVersionPrefixes("3.1.14", "3.1.13garbage"));
}

void testComparisonHandlesVersionBounds() {
  TEST_ASSERT_EQUAL_INT(1, pullOtaCompareVersionPrefixes("v4.0.0", "3.99.99"));
  TEST_ASSERT_EQUAL_INT(0, pullOtaCompareVersionPrefixes("65535.0.0", "65535.0.0-dev"));
  TEST_ASSERT_FALSE(pullOtaVersionIsStable("65536.0.0"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testStableVersionsStayStrict);
  RUN_TEST(testDevBuildUsesNumericPrefix);
  RUN_TEST(testComparisonHandlesVersionBounds);
  return UNITY_END();
}
