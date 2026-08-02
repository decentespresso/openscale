#include <string.h>
#include <unity.h>
#include "mdns_name.h"

void setUp() {}
void tearDown() {}

static bool normalize(const char *input, char *out) {
  return mdnsNameNormalize(input, out, MDNS_NAME_BUFFER_BYTES);
}

void testDefaultNameIsHds() {
  TEST_ASSERT_EQUAL_STRING("hds", mdnsNameDefault());
  TEST_ASSERT_TRUE(mdnsNameIsDefault("hds"));
  TEST_ASSERT_FALSE(mdnsNameIsDefault("hds2"));
  TEST_ASSERT_FALSE(mdnsNameIsDefault("hd"));
  TEST_ASSERT_FALSE(mdnsNameIsDefault("kitchen"));
}

void testCaseAndWhitespaceAreNormalized() {
  char out[MDNS_NAME_BUFFER_BYTES];
  TEST_ASSERT_TRUE(normalize("  Kitchen  ", out));
  TEST_ASSERT_EQUAL_STRING("kitchen", out);
  TEST_ASSERT_TRUE(normalize("\tHDS-2\r\n", out));
  TEST_ASSERT_EQUAL_STRING("hds-2", out);
}

void testEmptyInputRestoresDefault() {
  char out[MDNS_NAME_BUFFER_BYTES];
  TEST_ASSERT_TRUE(normalize("", out));
  TEST_ASSERT_EQUAL_STRING("hds", out);
  TEST_ASSERT_TRUE(normalize("   \t ", out));
  TEST_ASSERT_EQUAL_STRING("hds", out);
  TEST_ASSERT_TRUE(normalize(nullptr, out));
  TEST_ASSERT_EQUAL_STRING("hds", out);
}

void testIllegalCharactersAreRejected() {
  char out[MDNS_NAME_BUFFER_BYTES];
  TEST_ASSERT_FALSE(normalize("my scale", out));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(normalize("hds.local", out));
  TEST_ASSERT_FALSE(normalize("caf\xc3\xa9", out));
  TEST_ASSERT_FALSE(normalize("hds_2", out));
  TEST_ASSERT_FALSE(normalize("hds/2", out));
}

void testHyphenPlacementIsRejected() {
  char out[MDNS_NAME_BUFFER_BYTES];
  TEST_ASSERT_FALSE(normalize("-hds", out));
  TEST_ASSERT_FALSE(normalize("hds-", out));
  TEST_ASSERT_FALSE(normalize("-", out));
  TEST_ASSERT_TRUE(normalize("h-d-s", out));
  TEST_ASSERT_EQUAL_STRING("h-d-s", out);
}

void testLengthBoundary() {
  char out[MDNS_NAME_BUFFER_BYTES];
  char maxName[MDNS_NAME_MAX_CHARS + 1];
  memset(maxName, 'a', MDNS_NAME_MAX_CHARS);
  maxName[MDNS_NAME_MAX_CHARS] = 0;
  TEST_ASSERT_TRUE(normalize(maxName, out));
  TEST_ASSERT_EQUAL_STRING(maxName, out);

  char tooLong[MDNS_NAME_MAX_CHARS + 2];
  memset(tooLong, 'a', MDNS_NAME_MAX_CHARS + 1);
  tooLong[MDNS_NAME_MAX_CHARS + 1] = 0;
  TEST_ASSERT_FALSE(normalize(tooLong, out));
}

void testSingleCharacterNamesAreAllowed() {
  char out[MDNS_NAME_BUFFER_BYTES];
  TEST_ASSERT_TRUE(normalize("a", out));
  TEST_ASSERT_EQUAL_STRING("a", out);
  TEST_ASSERT_TRUE(normalize("7", out));
  TEST_ASSERT_EQUAL_STRING("7", out);
}

void testUndersizedOutputBufferFails() {
  char out[4];
  TEST_ASSERT_FALSE(mdnsNameNormalize("kitchen", out, sizeof(out)));
  TEST_ASSERT_TRUE(mdnsNameNormalize("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("hds", out);
  char tiny[2];
  TEST_ASSERT_FALSE(mdnsNameNormalize("", tiny, sizeof(tiny)));
}

void testOledSplitKeepsEveryCharacter() {
  char line1[MDNS_NAME_OLED_LINE1_BYTES];

  const char *maxName = "abcdefghijklmnopqrstuvwx";
  TEST_ASSERT_EQUAL_size_t(MDNS_NAME_MAX_CHARS, strlen(maxName));
  const char *line2 = mdnsNameSplitOledLine1(maxName, line1, sizeof(line1));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmn", line1);
  TEST_ASSERT_EQUAL_STRING("opqrstuvwx", line2);
  TEST_ASSERT_EQUAL_size_t(MDNS_NAME_OLED_LINE1_CHARS, strlen(line1));
  TEST_ASSERT_TRUE(strlen(line2) <= MDNS_NAME_OLED_LINE2_CHARS);

  char joined[MDNS_NAME_BUFFER_BYTES];
  strcpy(joined, line1);
  strcat(joined, line2);
  TEST_ASSERT_EQUAL_STRING(maxName, joined);
}

void testOledSplitShortNameHasNoSecondLine() {
  char line1[MDNS_NAME_OLED_LINE1_BYTES];
  const char *line2 = mdnsNameSplitOledLine1("hds", line1, sizeof(line1));
  TEST_ASSERT_EQUAL_STRING("hds", line1);
  TEST_ASSERT_EQUAL_STRING("", line2);

  line2 = mdnsNameSplitOledLine1("abcdefghijklmn", line1, sizeof(line1));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmn", line1);
  TEST_ASSERT_EQUAL_STRING("", line2);
}

void testOledSplitHandlesBadArguments() {
  char line1[MDNS_NAME_OLED_LINE1_BYTES];
  TEST_ASSERT_EQUAL_STRING("", mdnsNameSplitOledLine1(nullptr, line1, sizeof(line1)));
  TEST_ASSERT_EQUAL_STRING("", line1);
  TEST_ASSERT_EQUAL_STRING("", mdnsNameSplitOledLine1("hds", nullptr, 0));

  char tiny[4];
  const char *line2 = mdnsNameSplitOledLine1("abcdefghijklmnopqrstuvwx", tiny, sizeof(tiny));
  TEST_ASSERT_EQUAL_STRING("abc", tiny);
  TEST_ASSERT_EQUAL_STRING("defghijklmnopqrstuvwx", line2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testDefaultNameIsHds);
  RUN_TEST(testCaseAndWhitespaceAreNormalized);
  RUN_TEST(testEmptyInputRestoresDefault);
  RUN_TEST(testIllegalCharactersAreRejected);
  RUN_TEST(testHyphenPlacementIsRejected);
  RUN_TEST(testLengthBoundary);
  RUN_TEST(testSingleCharacterNamesAreAllowed);
  RUN_TEST(testUndersizedOutputBufferFails);
  RUN_TEST(testOledSplitKeepsEveryCharacter);
  RUN_TEST(testOledSplitShortNameHasNoSecondLine);
  RUN_TEST(testOledSplitHandlesBadArguments);
  return UNITY_END();
}
