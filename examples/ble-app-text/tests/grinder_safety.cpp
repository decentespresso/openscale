#include <cassert>
#include "grinder_protocol.h"
#include "grinder_reply_frame.h"
#include "integration_state.h"

#define HDS_ENABLE_GRINDER 1

enum GrinderDosingState { GRINDER_STATE_FINDING_PLUG, GRINDER_STATE_CONNECTED, GRINDER_STATE_GRINDING,
  GRINDER_STATE_STOPPING, GRINDER_STATE_AWAIT_REMOVAL, GRINDER_STATE_AWAIT_ZERO, GRINDER_STATE_ERROR };
enum GrinderPendingCommand { GRINDER_COMMAND_NONE, GRINDER_COMMAND_HELLO,
  GRINDER_COMMAND_PING, GRINDER_COMMAND_ON, GRINDER_COMMAND_OFF, GRINDER_COMMAND_BYE };
struct {
  bool relayOn = false, shutdownUnconfirmed = false, resumeAfterRecovery = false;
  bool tareRequestArmsGrinder = false, tareRearmRequested = false, userTareComplete = false, removalSeen = false;
  char activePlugMac[18] = {};
  unsigned long lastCommandAt = 0, lastPingAt = 0, stateEnteredAt = 0, lastRxAt = 0, offSentAt = 0;
  uint32_t lastConnectAttempt = 0, reconnectDelayMs = 0;
  uint8_t nextRequestId = 0, pendingRequestId = 0, missedPingResponses = 0;
  GrinderReplyFrame reply;
  float stopWeight = 0;
  GrinderDosingState state = GRINDER_STATE_CONNECTED;
  GrinderPendingCommand pendingCommand = GRINDER_COMMAND_NONE;
  GrinderRecoveryState recovery = grinderRecoveryComplete();
} grinderRuntime;
struct { char selectedMac[18] = "12:34:56:78:9A:BC"; } grinderSettings;
struct { template<class... T> void printf(const char *, T...) {} } Serial;
unsigned long now = 123;
bool b_ota = false, b_calibration = false;
bool connected = true;
int readCount = 0, connectAttempts = 0, offCount = 0;
const GrinderTcpResponse *queuedResponse = nullptr;
unsigned long millis() { return now; }
const char *grinderStateText(GrinderDosingState) { return "test"; }
bool grinderBleConnected() { return connected; }
void grinderBleClose() { connected = false; }
bool grinderBleWrite(const char *data, size_t length, uint8_t id) {
  assert(data && length != 0 && id != 0);
  return connected;
}
bool grinderBleBusy() { return false; }
bool grinderBleAvailable() { return true; }
void grinderBleStartScan(const char *) { ++connectAttempts; }
void grinderSetStatus(const char *) {}
void grinderSetState(GrinderDosingState state) { grinderRuntime.state = state; grinderRuntime.stateEnteredAt = now; }
void grinderCopyCString(char *target, size_t size, const char *source) { snprintf(target, size, "%s", source); }
void grinderClearDosingMetrics(float) {}
void grinderMarkAdaptiveShotOff(float) {}
void grinderWaitAfterRecovery() {}
void grinderDisconnectToFinding() {}
void grinderCloseClient() { @CLOSE@ }
bool grinderSendCommand(const char *data, size_t length, GrinderPendingCommand command) { @SEND@ }
bool grinderSendHello() { return grinderSendCommand("HELLO", 5, GRINDER_COMMAND_HELLO); }
bool grinderSendOff() { ++offCount; return grinderSendCommand("!", 1, GRINDER_COMMAND_OFF); }
void grinderEnterError(const char *status) { @ERROR@ }
void grinderHandleOkResponse(const GrinderTcpResponse &response, float weight) { @OK@ }

bool canRestart() { @GUARD@ }
bool grinderSelectedMacSet() { return true; }
void grinderReadClient(float weight) {
  ++readCount;
  if (queuedResponse) {
    const auto *response = queuedResponse;
    queuedResponse = nullptr;
    grinderHandleOkResponse(*response, weight);
  }
}
void grinderCheckConnectionLoss() {}
void grinderResolveAndConnect() { @RESOLVE@ }
void grinderTrackAdaptiveShot(float) {}
void grinderTickStopping(float weight) { @STOPPING@ }
void grinderSendPingIfDue() {}
void grinderTickWhileMenuOpen(float weight) { @MENU@ }

void startMenuHandshake() {
  grinderRuntime = {};
  connected = true;
  grinderRuntime.state = GRINDER_STATE_FINDING_PLUG;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  grinderRuntime.lastCommandAt = now;
}

void testMenuHandshake() {
  const GrinderTcpResponse off{};
  GrinderTcpResponse on{};
  on.relayOn = true;
  startMenuHandshake();
  assert(!canRestart());
  queuedResponse = &off;
  const int reads = readCount;
  grinderTickWhileMenuOpen(0);
  assert(readCount == reads + 1);
  assert(grinderRuntime.state == GRINDER_STATE_CONNECTED && canRestart());

  startMenuHandshake();
  queuedResponse = &on;
  const int stops = offCount;
  grinderTickWhileMenuOpen(0);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF && !canRestart());
  assert(offCount == stops + 1);
  queuedResponse = &off;
  grinderTickWhileMenuOpen(0);
  assert(canRestart() && !grinderRuntime.shutdownUnconfirmed);

  startMenuHandshake();
  grinderRuntime.shutdownUnconfirmed = true;
  now += 1201;
  grinderTickWhileMenuOpen(0);
  assert(!connected);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_NONE && !canRestart());
  grinderTickWhileMenuOpen(0);
  assert(connectAttempts == 0);
}

int main() {
  GrinderTcpResponse off{}, on{};
  off.relayOn = false;
  on.relayOn = true;
  assert(canRestart());
  assert(grinderSendCommand("ON\n", 3, GRINDER_COMMAND_ON));
  assert(grinderRuntime.shutdownUnconfirmed && !canRestart());
  grinderHandleOkResponse(on, 0);
  assert(grinderRuntime.state == GRINDER_STATE_GRINDING && !canRestart());
  grinderCloseClient();
  assert(!grinderRuntime.relayOn && grinderRuntime.shutdownUnconfirmed && !canRestart());
  connected = true;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  grinderHandleOkResponse(on, 0);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF && !canRestart());
  grinderHandleOkResponse(on, 0);
  assert(!canRestart());
  grinderEnterError("lost stop");
  assert(grinderRuntime.shutdownUnconfirmed && !canRestart());
  connected = true;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  grinderHandleOkResponse(off, 0);
  assert(canRestart());
  assert(grinderSendCommand("ON\n", 3, GRINDER_COMMAND_ON));
  grinderHandleOkResponse(on, 0);
  assert(grinderSendOff());
  grinderHandleOkResponse(off, 0);
  assert(!grinderRuntime.shutdownUnconfirmed && canRestart());
  connected = false;
  assert(!grinderSendCommand("ON\n", 3, GRINDER_COMMAND_ON));
  assert(canRestart());
  testMenuHandshake();
}
