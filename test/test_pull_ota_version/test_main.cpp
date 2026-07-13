#include <unity.h>
#include <string.h>
#include "pull_ota_catalog.h"
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

struct TestRelease {
  const char *key;
  const char *version;
};

bool sameRelease(const TestRelease &left, const TestRelease &right) {
  return strcmp(left.key, right.key) == 0 && strcmp(left.version, right.version) == 0;
}

int compareReleaseVersions(const TestRelease &left, const TestRelease &right) {
  return pullOtaCompareVersionPrefixes(left.version, right.version);
}

void testCatalogSortsDeduplicatesAndCaps() {
  TestRelease releases[3];
  uint8_t count = 0;
  TEST_ASSERT_TRUE(pullOtaCatalogAdd(releases, count, 3, {"hds", "3.1.14"}, sameRelease, compareReleaseVersions));
  TEST_ASSERT_TRUE(pullOtaCatalogAdd(releases, count, 3, {"hds", "3.1.16"}, sameRelease, compareReleaseVersions));
  TEST_ASSERT_TRUE(pullOtaCatalogAdd(releases, count, 3, {"hds", "3.1.15"}, sameRelease, compareReleaseVersions));
  TEST_ASSERT_FALSE(pullOtaCatalogAdd(releases, count, 3, {"hds", "3.1.15"}, sameRelease, compareReleaseVersions));
  TEST_ASSERT_FALSE(pullOtaCatalogAdd(releases, count, 3, {"hds", "3.1.13"}, sameRelease, compareReleaseVersions));
  TEST_ASSERT_EQUAL_UINT8(3, count);
  TEST_ASSERT_EQUAL_STRING("3.1.16", releases[0].version);
  TEST_ASSERT_EQUAL_STRING("3.1.15", releases[1].version);
  TEST_ASSERT_EQUAL_STRING("3.1.14", releases[2].version);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testStableVersionsStayStrict);
  RUN_TEST(testDevBuildUsesNumericPrefix);
  RUN_TEST(testComparisonHandlesVersionBounds);
  RUN_TEST(testCatalogSortsDeduplicatesAndCaps);
  return UNITY_END();
}
