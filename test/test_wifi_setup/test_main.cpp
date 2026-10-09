#include <limits.h>
#include <string.h>
#include <unity.h>
#include "wifi_credentials.h"
#include "wifi_switch.h"

void setUp() {}
void tearDown() {}

static bool normalize(const char *ssid, const char *pass, WifiCredentials &output) {
  return wifiNormalizeCredentials(ssid, strlen(ssid), pass, strlen(pass), output);
}

void testUnicodeEdgesAreRemoved() {
  WifiCredentials output;
  TEST_ASSERT_TRUE(normalize("\xc2\xa0\xe2\x80\x8b Kitchen \xef\xbb\xbf",
                             "\xe2\x81\xa0  secret12\r\n", output));
  TEST_ASSERT_EQUAL_STRING("Kitchen", output.ssid);
  TEST_ASSERT_EQUAL_STRING("secret12", output.pass);
  TEST_ASSERT_TRUE(normalize("\xf3\xa0\x80\x81net\xf3\xa0\x80\x81", "", output));
  TEST_ASSERT_EQUAL_STRING("net", output.ssid);
}

void testInteriorCharactersAndCaseArePreserved() {
  WifiCredentials output;
  TEST_ASSERT_TRUE(normalize("  Caf\xc3\xa9 WiFi  ", "  pass word  ", output));
  TEST_ASSERT_EQUAL_STRING("Caf\xc3\xa9 WiFi", output.ssid);
  TEST_ASSERT_EQUAL_STRING("pass word", output.pass);
  TEST_ASSERT_TRUE(normalize("a\xe2\x80\x8d" "b", "", output));
  TEST_ASSERT_EQUAL_STRING("a\xe2\x80\x8d" "b", output.ssid);
}

void testEmptyAndInvisibleSsidAreInvalid() {
  WifiCredentials output;
  TEST_ASSERT_FALSE(normalize("", "", output));
  TEST_ASSERT_FALSE(normalize(" \t\xef\xbb\xbf\xe2\x80\x8b", "", output));
}

void testInvalidUtf8AndControlsAreRejected() {
  WifiCredentials output;
  TEST_ASSERT_FALSE(normalize("bad\xc0\xaf", "", output));
  TEST_ASSERT_FALSE(normalize("bad\xed\xa0\x80", "", output));
  TEST_ASSERT_FALSE(normalize("bad\xf4\x90\x80\x80", "", output));
  TEST_ASSERT_FALSE(normalize("bad\xe2\x80", "", output));
  TEST_ASSERT_FALSE(normalize("bad\tname", "", output));
  TEST_ASSERT_FALSE(normalize("valid", "pass\x7fword", output));
  const char embedded[] = {'n', 0, 'x'};
  TEST_ASSERT_FALSE(wifiNormalizeCredentials(embedded, sizeof(embedded), "", 0, output));
}

void testByteLengthBoundaries() {
  WifiCredentials output;
  const char *maxSsid = "12345678901234567890123456789012";
  TEST_ASSERT_TRUE(normalize(maxSsid, "12345678", output));
  TEST_ASSERT_FALSE(normalize("123456789012345678901234567890123", "", output));
  TEST_ASSERT_FALSE(normalize("valid", "short", output));
  char pass[66];
  memset(pass, 'a', 64);
  pass[64] = 0;
  TEST_ASSERT_TRUE(normalize("valid", pass, output));
  pass[0] = 'z';
  TEST_ASSERT_FALSE(normalize("valid", pass, output));
  pass[63] = 0;
  TEST_ASSERT_TRUE(normalize("valid", pass, output));
  pass[63] = 'a';
  pass[64] = 'a';
  pass[65] = 0;
  TEST_ASSERT_FALSE(normalize("valid", pass, output));
  const char *wide = "\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc\xc3\xbc";
  TEST_ASSERT_TRUE(normalize(wide, "", output));
  char longer[35];
  strcpy(longer, wide);
  strcat(longer, "x");
  TEST_ASSERT_FALSE(normalize(longer, "", output));
}

void testCredentialRecordValidation() {
  WifiCredentials record;
  TEST_ASSERT_TRUE(wifiCredentialRecordValid(record));
  record.version = 2;
  TEST_ASSERT_FALSE(wifiCredentialRecordValid(record));
  record.version = 1;
  memset(record.ssid, 'x', sizeof(record.ssid));
  TEST_ASSERT_FALSE(wifiCredentialRecordValid(record));
  record.ssid[0] = 0;
  memset(record.pass, 'x', sizeof(record.pass));
  TEST_ASSERT_FALSE(wifiCredentialRecordValid(record));
}

void testOnlyStableConnectionCanBeSaved() {
  WifiSwitch change;
  change.phase = WifiSwitchPhase::Queued;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::None, (int)change.tick(499, false));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Connect, (int)change.tick(500, false));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::None, (int)change.tick(1000, true));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::None, (int)change.tick(3999, true));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Save, (int)change.tick(4000, true));
  change.saved(4000, true);
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchPhase::Succeeded, (int)change.phase);
  TEST_ASSERT_FALSE(change.busy());
}

void testFailureRestoresPreviousNetwork() {
  WifiSwitch change;
  change.hasPrevious = true;
  change.phase = WifiSwitchPhase::Connecting;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Restore,
      (int)change.tick(20000, false, WifiSetupError::Authentication));
  TEST_ASSERT_TRUE(change.busy());
  change.tick(21000, true);
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchPhase::Failed, (int)change.phase);
  TEST_ASSERT_EQUAL_INT((int)WifiSetupError::Authentication, (int)change.error);
}

void testStorageFailureAndUnstableLinkDoNotSucceed() {
  WifiSwitch change;
  change.hasPrevious = true;
  change.phase = WifiSwitchPhase::Saving;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Restore, (int)change.saved(5000, false));
  TEST_ASSERT_EQUAL_INT((int)WifiSetupError::Storage, (int)change.error);
  change.phase = WifiSwitchPhase::Verifying;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Restore, (int)change.tick(6000, false));
}

void testReconnectedLinkCannotCompleteStabilityCheck() {
  WifiSwitch change;
  change.hasPrevious = true;
  change.phase = WifiSwitchPhase::Connecting;
  change.tick(1000, true, WifiSetupError::Timeout, 4);
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::Restore,
      (int)change.tick(4000, true, WifiSetupError::Timeout, 5));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchPhase::Restoring, (int)change.phase);
}

void testUnavailablePreviousNetworkOpensSetup() {
  WifiSwitch change;
  change.phase = WifiSwitchPhase::Connecting;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::AccessPoint, (int)change.tick(20000, false));
  change.hasPrevious = true;
  change.phase = WifiSwitchPhase::Restoring;
  change.startedAt = 20000;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::AccessPoint, (int)change.tick(40000, false));
}

void testTimeoutWorksAcrossMillisRollover() {
  WifiSwitch change;
  change.phase = WifiSwitchPhase::Connecting;
  change.startedAt = ULONG_MAX - 10000;
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::None, (int)change.tick(9998, false));
  TEST_ASSERT_EQUAL_INT((int)WifiSwitchAction::AccessPoint, (int)change.tick(9999, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testUnicodeEdgesAreRemoved);
  RUN_TEST(testInteriorCharactersAndCaseArePreserved);
  RUN_TEST(testEmptyAndInvisibleSsidAreInvalid);
  RUN_TEST(testInvalidUtf8AndControlsAreRejected);
  RUN_TEST(testByteLengthBoundaries);
  RUN_TEST(testCredentialRecordValidation);
  RUN_TEST(testOnlyStableConnectionCanBeSaved);
  RUN_TEST(testFailureRestoresPreviousNetwork);
  RUN_TEST(testStorageFailureAndUnstableLinkDoNotSucceed);
  RUN_TEST(testReconnectedLinkCannotCompleteStabilityCheck);
  RUN_TEST(testUnavailablePreviousNetworkOpensSetup);
  RUN_TEST(testTimeoutWorksAcrossMillisRollover);
  return UNITY_END();
}
