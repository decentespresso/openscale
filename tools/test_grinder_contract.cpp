#include <assert.h>
#include <math.h>

#include "grinder_protocol.h"
#include "grinder_adaptive_safety.h"

int main() {
  GrinderTcpResponse response;
  assert(grinderParseResponse("OK A4:C1:38:12:34:56 state=OFF", &response));
  assert(response.kind == GRINDER_TCP_RESPONSE_OK && !response.relayOn);
  assert(grinderParseResponse("OK A4:C1:38:12:34:56 state=ON", &response));
  assert(response.relayOn);
  assert(grinderParseResponse("BUSY A4:C1:38:12:34:56", &response));
  assert(grinderParseResponse("ERR A4:C1:38:12:34:56 reason=bad_mac", &response));
  assert(!grinderParseResponse("", &response));
  assert(!grinderParseResponse("OK A4 state=ON", &response));
  assert(!grinderParseResponse("ERR A4 reason=x", &response));

  assert(grinderNormalizeTargetGrams(9.9f) == GRINDER_TARGET_MIN_GRAMS);
  assert(grinderNormalizeTargetGrams(200.1f) == GRINDER_TARGET_MAX_GRAMS);
  assert(grinderNormalizeSafetyGrams(12.0f, 10.0f) == 5.0f);
  assert(grinderNormalizeSafetyGrams(-1.0f, 15.0f) == 0.0f);
  assert(grinderCutoffGrams(15.0f, 2.0f) == 13.0f);

  bool guardActive = false;
  uint32_t zeroExitAt = 0;
  bool setupMassBlocked = false;
  assert(!grinderCutoffShouldStop(2.0f, 1.0f, 15.0f, 2.0f, false, 100, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!grinderCutoffShouldStop(8.0f, 1.0f, 15.0f, 2.0f, false, 1100, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(grinderCutoffShouldStop(13.0f, 1.0f, 15.0f, 2.0f, false, 2100, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!grinderCutoffShouldStop(0.0f, 1.0f, 15.0f, 2.0f, false, 2200, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!guardActive && !setupMassBlocked && zeroExitAt == 0);
  assert(!grinderCutoffShouldStop(2.0f, 1.0f, 15.0f, 2.0f, false, 3000, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!grinderCutoffShouldStop(13.0f, 1.0f, 15.0f, 2.0f, false, 4500, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(setupMassBlocked);
  assert(!grinderCutoffShouldStop(13.0f, 1.0f, 15.0f, 2.0f, false, 7000, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!grinderCutoffShouldStop(0.0f, 1.0f, 15.0f, 2.0f, false, 7100, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(!grinderCutoffShouldStop(13.0f, 1.0f, 15.0f, 2.0f, false, 8000, &guardActive, &zeroExitAt, &setupMassBlocked));
  assert(setupMassBlocked);
  assert(grinderCanArmAfterTare(true, true));
  assert(!grinderCanArmAfterTare(false, true));
  assert(!grinderHeartbeatLost(2, 1000, 2500));
  assert(grinderHeartbeatLost(3, 1000, 2500));
  assert(grinderHeartbeatLost(0, 1000, 2750));
  GrinderAdaptiveShot shot;
  grinderAdaptiveShotReset(&shot);
  grinderAdaptiveShotTrack(&shot, 2.0f, 1.0f, 15.0f, 1);
  grinderAdaptiveShotTrack(&shot, 2.0f, 1.0f, 15.0f, 2101);
  assert(!shot.valid && shot.skipReason == GRINDER_ADAPTIVE_SKIP_STALL);

  GrinderAdaptiveSafetyStore store;
  store.history[0] = 12.0f;
  store.count = 1;
  grinderAdaptiveSafetyNormalize(&store, 2.0f, 9.0f);
  assert(store.history[0] == 9.0f);
  assert(grinderAdaptiveSafetyRecord(&store, 12.0f, 2.0f, 9.0f) <= 9.0f);
  return 0;
}
