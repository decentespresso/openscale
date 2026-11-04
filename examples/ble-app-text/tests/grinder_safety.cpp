#include <cassert>
#include "grinder_protocol.h"
#include "integration_state.h"

#define HDS_ENABLE_GRINDER 1

enum GrinderDosingState { GRINDER_STATE_FINDING_PLUG, GRINDER_STATE_CONNECTED, GRINDER_STATE_GRINDING,
  GRINDER_STATE_STOPPING, GRINDER_STATE_AWAIT_REMOVAL, GRINDER_STATE_AWAIT_ZERO, GRINDER_STATE_ERROR };
enum GrinderPendingCommand { GRINDER_COMMAND_NONE, GRINDER_COMMAND_HELLO,
  GRINDER_COMMAND_PING, GRINDER_COMMAND_ON, GRINDER_COMMAND_OFF, GRINDER_COMMAND_BYE };
struct Client {
  bool live = true;
  bool connected() { return live; }
  void stop() { live = false; }
  template<class T> void print(T) {}
};
struct {
  Client client;
  bool relayOn = false, shutdownUnconfirmed = false, resumeAfterRecovery = false;
  bool tareRequestArmsGrinder = false, tareRearmRequested = false, userTareComplete = false, removalSeen = false;
  char activePlugMac[18] = {};
  unsigned long lastCommandAt = 0, lastPingAt = 0, stateEnteredAt = 0;
  uint32_t lastConnectAttempt = 0, reconnectDelayMs = 0;
  uint8_t resolvePhase = 0;
  float stopWeight = 0;
  GrinderDosingState state = GRINDER_STATE_CONNECTED;
  GrinderPendingCommand pendingCommand = GRINDER_COMMAND_NONE;
  GrinderRecoveryState recovery = grinderRecoveryComplete();
} grinderRuntime;
struct { int lastIp = 1; } grinderSettings;
struct { template<class... T> void printf(const char *, T...) {} } Serial;
unsigned long now = 123;
bool b_ota = false, b_calibration = false;
int readCount = 0, connectAttempts = 0, offCount = 0;
const GrinderTcpResponse *queuedResponse = nullptr;
unsigned long millis() { return now; }
const char *grinderStateText(GrinderDosingState) { return "test"; }
void grinderResetLineReader() {}
void grinderSetStatus(const char *) {}
void grinderSetState(GrinderDosingState state) { grinderRuntime.state = state; grinderRuntime.stateEnteredAt = now; }
void grinderCopyCString(char *target, size_t size, const char *source) { snprintf(target, size, "%s", source); }
void grinderClearDosingMetrics(float) {}
void grinderMarkAdaptiveShotOff(float) {}
void grinderWaitAfterRecovery() {}
void grinderDisconnectToFinding() {}
void grinderCloseClient() { @CLOSE@ }
bool grinderSendLine(const char *line, GrinderPendingCommand command) { @SEND@ }
bool grinderSendOff() { ++offCount; return grinderSendLine("OFF", GRINDER_COMMAND_OFF); }
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
bool grinderAttemptConnect(int) { ++connectAttempts; return true; }
bool grinderAttemptHostnameConnect() { ++connectAttempts; return true; }
void grinderResolveAndConnect() { @RESOLVE@ }
void grinderTrackAdaptiveShot(float) {}
void grinderTickStopping(float weight) { @STOPPING@ }
void grinderSendPingIfDue() {}
void grinderTickWhileMenuOpen(float weight) { @MENU@ }

void startMenuHandshake() {
  grinderRuntime = {};
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
  assert(!grinderRuntime.client.live);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_NONE && !canRestart());
  grinderTickWhileMenuOpen(0);
  assert(connectAttempts == 0);
}

int main() {
  GrinderTcpResponse off{}, on{};
  off.relayOn = false;
  on.relayOn = true;
  assert(canRestart());
  assert(grinderSendLine("ON", GRINDER_COMMAND_ON));
  assert(grinderRuntime.shutdownUnconfirmed && !canRestart());
  grinderHandleOkResponse(on, 0);
  assert(grinderRuntime.state == GRINDER_STATE_GRINDING && !canRestart());
  grinderCloseClient();
  assert(!grinderRuntime.relayOn && grinderRuntime.shutdownUnconfirmed && !canRestart());
  grinderRuntime.client.live = true;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  grinderHandleOkResponse(on, 0);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF && !canRestart());
  grinderHandleOkResponse(on, 0);
  assert(!canRestart());
  grinderEnterError("lost stop");
  assert(grinderRuntime.shutdownUnconfirmed && !canRestart());
  grinderRuntime.client.live = true;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_HELLO;
  grinderHandleOkResponse(off, 0);
  assert(canRestart());
  assert(grinderSendLine("ON", GRINDER_COMMAND_ON));
  grinderHandleOkResponse(on, 0);
  assert(grinderSendOff());
  grinderHandleOkResponse(off, 0);
  assert(!grinderRuntime.shutdownUnconfirmed && canRestart());
  grinderRuntime.client.live = false;
  assert(!grinderSendLine("ON", GRINDER_COMMAND_ON));
  assert(canRestart());
  testMenuHandshake();
}
