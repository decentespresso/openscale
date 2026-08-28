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

void testWifiUpdateFramesBareRequestAsTwoBytes() {
  const uint8_t bare[] = {0x03, 0x1B};
  TEST_ASSERT_EQUAL_size_t(2, decentCommandFrameLength(bare, sizeof(bare), false));
}

void testWifiUpdateFramesBiasedVersionPayload() {
  const uint8_t targeted[] = {0x03, 0x1B, 0x83, 0x81, 0x8D};
  TEST_ASSERT_EQUAL_size_t(5, decentCommandFrameLength(targeted, sizeof(targeted), false));
}

void testWifiUpdateWaitsForAnIncompletePayload() {
  const uint8_t partial[] = {0x03, 0x1B, 0x83};
  TEST_ASSERT_EQUAL_size_t(0, decentCommandFrameLength(partial, sizeof(partial), false));
}

void testWifiUpdateDoesNotSwallowAFollowingCommand() {
  const uint8_t frames[] = {0x03, 0x1B, 0x03, 0x22};
  const size_t firstLength = decentCommandFrameLength(frames, sizeof(frames), false);
  TEST_ASSERT_EQUAL_size_t(2, firstLength);
  TEST_ASSERT_EQUAL_size_t(
      2,
      decentCommandFrameLength(frames + firstLength, sizeof(frames) - firstLength, false));
}

void testWifiUpdateDecodesBiasedVersionBytes() {
  const uint8_t payload[] = {0x83, 0x81, 0x8D};
  const PullOtaTargetVersion target = pullOtaTargetFromBiasedBytes(payload);
  TEST_ASSERT_TRUE(target.present);
  TEST_ASSERT_EQUAL_UINT8(3, target.major);
  TEST_ASSERT_EQUAL_UINT8(1, target.minor);
  TEST_ASSERT_EQUAL_UINT8(13, target.patch);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(testUsbWeightRejectsIncompleteFrames);
  RUN_TEST(testUsbWeightConsumesThreeByteCommands);
  RUN_TEST(testUsbWeightEnableSupportsOptionalInterval);
  RUN_TEST(testUsbWeightRetainsAdjacentFrameBoundary);
  RUN_TEST(testWifiUpdateFramesBareRequestAsTwoBytes);
  RUN_TEST(testWifiUpdateFramesBiasedVersionPayload);
  RUN_TEST(testWifiUpdateWaitsForAnIncompletePayload);
  RUN_TEST(testWifiUpdateDoesNotSwallowAFollowingCommand);
  RUN_TEST(testWifiUpdateDecodesBiasedVersionBytes);
  return UNITY_END();
}
