import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <cassert>
#include <initializer_list>
#include <limits>
#include "grinder_reply_frame.h"

enum Command { GRINDER_COMMAND_NONE, GRINDER_COMMAND_OFF };
struct {
  bool menuPaused = false;
  bool shutdownUnconfirmed = false;
  bool userTareComplete = true;
  Command pendingCommand = GRINDER_COMMAND_NONE;
} grinderRuntime;
struct { void println(const char *) {} } Serial;
float f_displayedValue = 12;
unsigned long clockMs = 0, connectedAt = 0;
bool connected = true, sendSucceeds = true, sends = false, replies = true, drops = false;
unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += ms; }
bool grinderBleConnected() { return connected && (!drops || clockMs - connectedAt < 15); }
void grinderBleTick() {}
bool grinderSendOff() {
  assert(grinderRuntime.menuPaused);
  sends = true;
  if (sendSucceeds) grinderRuntime.pendingCommand = GRINDER_COMMAND_OFF;
  return sendSucceeds;
}
void grinderReadClient(float weight) {
  assert(weight == 12);
  if (replies && clockMs - connectedAt >= 20) {
    grinderRuntime.shutdownUnconfirmed = false;
    grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
  }
}
void grinderRuntimeReset() {
  connected = false;
  grinderRuntime.userTareComplete = false;
  grinderRuntime.pendingCommand = GRINDER_COMMAND_NONE;
}
void grinderResumeAfterMenu() { grinderRuntime.menuPaused = false; }
#include "grinder_shutdown.h"

void checkShutdown(bool send, unsigned long elapsed, bool unconfirmed) {
  grinderPrepareForShutdown();
  assert(clockMs - connectedAt == elapsed);
  assert(sends == send && grinderRuntime.shutdownUnconfirmed == unconfirmed);
  assert(!connected && grinderRuntime.menuPaused && !grinderRuntime.userTareComplete);
  grinderResumeAfterCancelledShutdown();
  assert(!grinderRuntime.menuPaused);
}

void reset(bool unconfirmed, unsigned long now = 0) {
  grinderRuntime = {};
  grinderRuntime.shutdownUnconfirmed = unconfirmed;
  clockMs = connectedAt = now;
  connected = sendSucceeds = replies = true;
  sends = drops = false;
}

int main() {
  GrinderReplyFrame frame;
  using R = GrinderReplyResult;
  assert(frame.append(8, 8, "OK AA:BB:CC:DD:EE:", 18) == R::Partial);
  assert(frame.append(8, 7, "state=OFF\n", 10) == R::Ignored);
  assert(frame.append(8, 8, "FF state=OFF\n", 13) == R::Complete);
  GrinderTcpResponse reply;
  assert(grinderParseResponse(frame.line, &reply) && !reply.relayOn);
  assert(frame.append(8, 0, "BUSY AA:BB:CC:DD:", 17) == R::Partial);
  assert(frame.append(8, 0, "EE:FF\n", 6) == R::Complete);
  assert(grinderParseResponse(frame.line, &reply) && reply.kind == GRINDER_TCP_RESPONSE_BUSY);
  assert(frame.append(8, 8, "OK\nX", 4) == R::Invalid);
  assert(frame.append(8, 8, "\rX", 2) == R::Invalid);

  for (const bool unconfirmed : {false, true}) {
    reset(unconfirmed);
    checkShutdown(true, 20, false);
    reset(unconfirmed, std::numeric_limits<unsigned long>::max() - 100);
    replies = false;
    checkShutdown(true, 250, unconfirmed);
    reset(unconfirmed);
    sendSucceeds = false;
    checkShutdown(true, 0, unconfirmed);
    reset(unconfirmed);
    drops = true;
    checkShutdown(true, 15, unconfirmed);
    reset(unconfirmed);
    connected = false;
    checkShutdown(false, 0, unconfirmed);
  }
}
'''


def main():
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="grinder-shutdown-") as directory:
        cpp = Path(directory) / "shutdown.cpp"
        binary = Path(directory) / ("shutdown.exe" if os.name == "nt" else "shutdown")
        cpp.write_text(SOURCE, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Grinder BLE reply correlation and bounded shutdown passed")


if __name__ == "__main__":
    main()
