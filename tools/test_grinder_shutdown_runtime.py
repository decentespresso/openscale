import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    shutdown = (ROOT / "include/grinder_shutdown.h").read_text(encoding="utf-8")
    runtime = (ROOT / "include/grinder_runtime.h").read_text(encoding="utf-8")
    reset = block_after(runtime, "static inline void grinderRuntimeReset()")
    assert "grinderRuntime.userTareComplete = false;" in reset
    assert "grinderRuntime.menuPaused =" not in reset
    source = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <limits>
#include <string>
#include "grinder_protocol.h"
constexpr int MSG_DONTWAIT = 1, GRINDER_COMMAND_OFF = 4;
unsigned long clockMs = 0, responseAt = 20;
unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += ms; }
float f_displayedValue = 12;
bool blocked = false, failed = false, flood = false, drop = false;
bool acknowledged = false;
size_t writeLimit = 4;
std::string written, reply;
struct Client {
  bool live = true;
  size_t position = 0;
  bool connected() { return live && !(drop && clockMs >= 15); }
  int fd() { return 7; }
  int available() { return flood || (clockMs >= responseAt && position < reply.size()); }
  int read() { ++clockMs; return flood ? 'x' : reply[position++]; }
};
struct {
  Client client;
  bool menuPaused = false, userTareComplete = true;
  int pendingCommand = 0;
  char rxLine[GRINDER_TCP_MAX_LINE_LENGTH + 1] = {};
  uint16_t rxLength = 0;
  bool pendingCr = false;
} grinderRuntime;
struct { char selectedMac[18] = "AA:BB:CC:DD:EE:FF"; } grinderSettings;
int send(int fd, const void *data, size_t length, int flags) {
  assert(fd == 7 && flags == MSG_DONTWAIT && grinderRuntime.menuPaused);
  if (failed || blocked) { errno = failed ? EPIPE : EAGAIN; return -1; }
  const size_t count = std::min(length, writeLimit);
  written.append(static_cast<const char *>(data), count);
  return static_cast<int>(count);
}
void grinderRuntimeReset() {
  grinderRuntime.client.live = false;
  grinderRuntime.userTareComplete = false;
}
void grinderHandleOkResponse(const GrinderTcpResponse &response, float weight) {
  assert(response.kind == GRINDER_TCP_RESPONSE_OK && !response.relayOn);
  assert(grinderRuntime.pendingCommand == GRINDER_COMMAND_OFF && weight == 12);
  acknowledged = true;
}
struct { void println(const char *) {} } Serial;
void grinderResetLineReader() { @RESET_LINE@ }
bool grinderSelectedMacSet() { @SELECTED@ }
bool grinderResponseMatchesSelection(const GrinderTcpResponse &response) { @MATCH@ }
bool grinderLineRead(char value) { @READ_LINE@ }
void grinderPrepareForShutdown() { @SHUTDOWN@ }
void reset() {
  grinderRuntime.client = {};
  grinderRuntime.menuPaused = false;
  grinderRuntime.userTareComplete = true;
  grinderResetLineReader();
  clockMs = 0;
  responseAt = 20;
  blocked = failed = flood = drop = acknowledged = false;
  writeLimit = 4;
  written.clear();
  reply = "OK AA:BB:CC:DD:EE:FF state=OFF\n";
}
void checkClosed() {
  assert(!grinderRuntime.client.live && grinderRuntime.menuPaused);
  assert(!grinderRuntime.userTareComplete);
}
int main() {
  for (size_t limit : {size_t(1), size_t(2), size_t(4)}) {
    reset();
    writeLimit = limit;
    grinderPrepareForShutdown();
    assert(acknowledged && clockMs < 250 && written == "OFF\n");
    checkClosed();
  }
  for (const std::string &response : {std::string(),
       std::string("OK 00:11:22:33:44:55 state=OFF\n"),
       std::string("OK AA:BB:CC:DD:EE:FF state=ON\n")}) {
    reset();
    reply = response;
    grinderPrepareForShutdown();
    assert(!acknowledged && clockMs == 250 && written == "OFF\n");
    checkClosed();
  }
  reset();
  reply = "OK 00:11:22:33:44:55 state=OFF\nOK AA:BB:CC:DD:EE:FF state=OFF\n";
  grinderPrepareForShutdown();
  assert(acknowledged && clockMs < 250);
  checkClosed();
  for (int fault : {0, 1, 2, 3}) {
    reset();
    blocked = fault == 0;
    failed = fault == 1;
    flood = fault == 2;
    drop = fault == 3;
    grinderPrepareForShutdown();
    assert(!acknowledged && clockMs <= 250);
    assert(fault != 0 || written.empty());
    checkClosed();
  }
  reset();
  grinderRuntime.client.live = false;
  grinderPrepareForShutdown();
  assert(written.empty() && clockMs == 0);
  checkClosed();
  reset();
  const unsigned long startedAt = std::numeric_limits<unsigned long>::max() - 100;
  clockMs = startedAt;
  reply.clear();
  grinderPrepareForShutdown();
  assert(clockMs - startedAt == 250);
  checkClosed();
}
'''
    replacements = {
        "RESET_LINE": block_after(runtime, "static inline void grinderResetLineReader()"),
        "SELECTED": block_after(runtime, "static inline bool grinderSelectedMacSet()"),
        "MATCH": block_after(runtime, "static inline bool grinderResponseMatchesSelection("),
        "READ_LINE": block_after(runtime, "static inline bool grinderLineRead(char value)"),
        "SHUTDOWN": block_after(shutdown, "inline void grinderPrepareForShutdown()"),
    }
    for marker, body in replacements.items():
        source = source.replace(f"@{marker}@", body)
    compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    assert compiler, "A C++ compiler is required"
    with tempfile.TemporaryDirectory(prefix="grinder-shutdown-") as directory:
        cpp = Path(directory) / "shutdown.cpp"
        binary = Path(directory) / ("shutdown.exe" if os.name == "nt" else "shutdown")
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "include"), str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("Grinder OFF confirmation, bounded failures, partial writes, disconnect and rollover passed")


if __name__ == "__main__":
    main()
