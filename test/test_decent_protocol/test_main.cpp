#include <unity.h>
#include "decent_protocol_frame.h"

void setUp() {}
void tearDown() {}

void testUsbWeightRejectsIncompleteFrames() {
  const uint8_t header[] = {0x03, 0x20};
  TEST_ASSERT_EQUAL_size_t(0, decentCommandFrameLength(nullptr, 0, false));
  TEST_ASSERT_EQUAL_size_t(0, decentCommandFrameLength(header, sizeof(header), false));
}

void testUsbWeightConsumesThreeByteCommands() {
  const uint8_t disable[] = {0x03, 0x20, 0x00};
  const uint8_t other[] = {0x03, 0x20, 0x02};
  TEST_ASSERT_EQUAL_size_t(3, decentCommandFrameLength(disable, sizeof(disable), false));
  TEST_ASSERT_EQUAL_size_t(3, decentCommandFrameLength(other, sizeof(other), false));
}

void testUsbWeightEnableSupportsOptionalInterval() {
  const uint8_t shortEnable[] = {0x03, 0x20, 0x01};
  const uint8_t intervalEnable[] = {0x03, 0x20, 0x01, 0x05};
  TEST_ASSERT_EQUAL_size_t(0, decentCommandFrameLength(shortEnable, sizeof(shortEnable), false));
  TEST_ASSERT_EQUAL_size_t(3, decentCommandFrameLength(shortEnable, sizeof(shortEnable), true));
  TEST_ASSERT_EQUAL_size_t(4, decentCommandFrameLength(intervalEnable, sizeof(intervalEnable), false));
  TEST_ASSERT_EQUAL_size_t(4, decentCommandFrameLength(intervalEnable, sizeof(intervalEnable), true));
}

void testUsbWeightRetainsAdjacentFrameBoundary() {
  const uint8_t frames[] = {0x03, 0x20, 0x00, 0x03, 0x20, 0x01, 0x05};
  const size_t firstLength = decentCommandFrameLength(frames, sizeof(frames), false);
  TEST_ASSERT_EQUAL_size_t(3, firstLength);
  TEST_ASSERT_EQUAL_size_t(
      4,
      decentCommandFrameLength(frames + firstLength, sizeof(frames) - firstLength, false));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testUsbWeightRejectsIncompleteFrames);
  RUN_TEST(testUsbWeightConsumesThreeByteCommands);
  RUN_TEST(testUsbWeightEnableSupportsOptionalInterval);
  RUN_TEST(testUsbWeightRetainsAdjacentFrameBoundary);
  return UNITY_END();
}
