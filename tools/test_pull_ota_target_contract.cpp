#include <assert.h>
#include <string.h>

#include "pull_ota_target.h"

static bool parses(const char *text, uint8_t major, uint8_t minor, uint8_t patch) {
  PullOtaTargetVersion target;
  if (!pullOtaParseTargetVersion(text, strlen(text), target)) {
    return false;
  }
  return target.present && target.major == major && target.minor == minor &&
         target.patch == patch;
}

static bool rejects(const char *text) {
  PullOtaTargetVersion target;
  return !pullOtaParseTargetVersion(text, strlen(text), target);
}

int main() {
  PullOtaTargetVersion absent = pullOtaNoTargetVersion();
  assert(!absent.present);

  char buffer[HDS_OTA_TARGET_VERSION_BUFFER_BYTES];
  assert(!pullOtaFormatTargetVersion(absent, buffer, sizeof(buffer)));

  assert(pullOtaFormatTargetVersion(
      pullOtaMakeTargetVersion(3, 1, 13), buffer, sizeof(buffer)));
  assert(strcmp(buffer, "3.1.13") == 0);

  assert(pullOtaFormatTargetVersion(
      pullOtaMakeTargetVersion(127, 127, 127), buffer, sizeof(buffer)));
  assert(strcmp(buffer, "127.127.127") == 0);

  assert(parses("3.1.13", 3, 1, 13));
  assert(parses("v3.1.13", 3, 1, 13));
  assert(parses("V3.1.15", 3, 1, 15));
  assert(parses("  3.1.13  ", 3, 1, 13));
  assert(parses("0.0.0", 0, 0, 0));
  assert(parses("127.127.127", 127, 127, 127));

  assert(rejects(""));
  assert(rejects("   "));
  assert(rejects("3.1"));
  assert(rejects("3"));
  assert(rejects("3.1.15-rc1"));
  assert(rejects("3.1.13-dev"));
  assert(rejects("v3.1.15.2"));
  assert(rejects("3.1."));
  assert(rejects(".1.13"));
  assert(rejects("3..13"));
  assert(rejects("128.1.13"));
  assert(rejects("3.128.13"));
  assert(rejects("3.1.128"));
  assert(rejects("3.1.13x"));
  assert(rejects("v"));

  PullOtaTargetVersion roundTrip;
  assert(pullOtaFormatTargetVersion(
      pullOtaMakeTargetVersion(3, 1, 13), buffer, sizeof(buffer)));
  assert(pullOtaParseTargetVersion(buffer, strlen(buffer), roundTrip));
  assert(roundTrip.present && roundTrip.major == 3 && roundTrip.minor == 1 &&
         roundTrip.patch == 13);

  assert(pullOtaEncodeTargetByte(3) == 0x83);
  assert(pullOtaEncodeTargetByte(1) == 0x81);
  assert(pullOtaEncodeTargetByte(13) == 0x8D);
  assert(pullOtaEncodeTargetByte(0) == 0x80);
  assert(pullOtaEncodeTargetByte(127) == 0xFF);

  assert(!pullOtaTargetByteIsBiased(0x03));
  assert(!pullOtaTargetByteIsBiased(0x1B));
  assert(!pullOtaTargetByteIsBiased(0x7F));
  assert(pullOtaTargetByteIsBiased(0x80));
  assert(pullOtaTargetByteIsBiased(0x83));
  assert(pullOtaTargetByteIsBiased(0xFF));

  const uint8_t biased[3] = {0x83, 0x81, 0x8D};
  PullOtaTargetVersion decoded = pullOtaTargetFromBiasedBytes(biased);
  assert(decoded.present && decoded.major == 3 && decoded.minor == 1 &&
         decoded.patch == 13);

  return 0;
}
