import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]

SOURCE = r'''
#include <cassert>
#include <limits>
#include "grinder_reply_frame.h"

struct {
  bool menuPaused = false;
  bool shutdownUnconfirmed = false;
  bool userTareComplete = true;
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
  assert(grinderRuntime.menuPaused && grinderRuntime.shutdownUnconfirmed);
  sends = true;
  return sendSucceeds;
}
void grinderReadClient(float weight) {
  assert(weight == 12);
  if (replies && clockMs - connectedAt >= 20) grinderRuntime.shutdownUnconfirmed = false;
}
void grinderRuntimeReset() {
  connected = false;
  grinderRuntime.userTareComplete = false;
}
void grinderResumeAfterMenu() { grinderRuntime.menuPaused = false; }
#include "grinder_shutdown.h"

void checkShutdown(bool send, unsigned long elapsed, bool confirmed) {
  grinderPrepareForShutdown();
  assert(clockMs - connectedAt == elapsed);
  assert(sends == send && grinderRuntime.shutdownUnconfirmed == !confirmed);
  assert(!connected && grinderRuntime.menuPaused && !grinderRuntime.userTareComplete);
  grinderResumeAfterCancelledShutdown();
  assert(!grinderRuntime.menuPaused);
}

void reset(unsigned long now = 0) {
  grinderRuntime = {};
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

  reset();
  checkShutdown(true, 20, true);
  reset(std::numeric_limits<unsigned long>::max() - 100);
  replies = false;
  checkShutdown(true, 250, false);
  reset();
  sendSucceeds = false;
  checkShutdown(true, 0, false);
  reset();
  drops = true;
  checkShutdown(true, 15, false);
  reset();
  connected = false;
  checkShutdown(false, 0, true);
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
