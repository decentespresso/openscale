from pathlib import Path
import shutil
import subprocess
import tempfile

from test_ota_runtime_isolation_contract import function_body


ROOT = Path(__file__).resolve().parents[1]


def main():
    timeout = function_body(
        (ROOT / "include" / "wifi_ota.h").read_text(encoding="utf-8"),
        "processElegantOtaTimeout",
    )
    source = r'''
#include <cassert>
#include <limits>

bool b_ota = true;
bool b_pullOtaRunning = false;
bool locked = false;
bool publishBeforeLock = false;
bool resetQueued = false;
int otaDisplayMux = 0;
unsigned long clockMs = 0;
unsigned long otaActivityAt = 0;
unsigned long resetAt = 0;
constexpr unsigned long OTA_ACTIVITY_TIMEOUT_MS = 30000;

unsigned long millis() { return clockMs; }

void portENTER_CRITICAL(int *) {
  assert(!locked);
  if (publishBeforeLock) {
    ++clockMs;
    otaActivityAt = clockMs;
  }
  locked = true;
}

void portEXIT_CRITICAL(int *) {
  assert(locked);
  locked = false;
}

struct {
  void println(const char *) { assert(!locked); }
} Serial;

void remoteQueueOtaResetAt(unsigned long now) {
  assert(!locked);
  resetQueued = true;
  resetAt = now;
}

void processElegantOtaTimeout() {
@TIMEOUT@
}

void checkTimeout(unsigned long now, unsigned long activityAt, bool publish,
                  bool expectedReset, bool ota = true, bool pull = false) {
  clockMs = now;
  otaActivityAt = activityAt;
  publishBeforeLock = publish;
  resetQueued = false;
  b_ota = ota;
  b_pullOtaRunning = pull;
  processElegantOtaTimeout();
  assert(!locked);
  assert(resetQueued == expectedReset);
  if (expectedReset) assert(resetAt == clockMs);
}

int main() {
  checkTimeout(5000, 4999, true, false);
  checkTimeout(5000, 4500, false, false);
  checkTimeout(30999, 1000, false, false);
  checkTimeout(31000, 1000, false, true);
  checkTimeout(31000, 1000, false, false, false);
  checkTimeout(31000, 1000, false, false, true, true);
  const unsigned long maximum = std::numeric_limits<unsigned long>::max();
  checkTimeout(5, maximum - 100, false, false);
  checkTimeout(5, maximum - 30000, false, true);
  checkTimeout(maximum, maximum - 1, true, false);
}
'''.replace("@TIMEOUT@", timeout)
    compiler = shutil.which("g++")
    assert compiler, "g++ is required"
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "ota_timeout.cpp"
        binary = Path(directory) / "ota_timeout.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run(
            [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
             str(cpp), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("OTA timeout snapshot and unsigned-wrap runtime tests passed")


if __name__ == "__main__":
    main()
