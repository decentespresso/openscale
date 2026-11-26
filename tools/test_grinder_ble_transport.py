from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def test_grinder_ble_transport():
    compiler = shutil.which("g++") or shutil.which("clang++")
    assert compiler, "a host C++ compiler is required"
    source = r'''
#include <cassert>
#include "grinder_ble_transport_state.h"

int main() {
  constexpr uint8_t advertisement[] = {0xff, 0xff, 0x01, 0x2b, 0x30, 0x45, 0xfe, 0x00};
  char mac[18];
  assert(grinderBleAdvertisedMac(advertisement, sizeof(advertisement), mac));
  assert(strcmp(mac, "01:2B:30:45:FE:00") == 0);
  assert(!grinderBleAdvertisedMac(advertisement, sizeof(advertisement) - 1, mac));

  GrinderBleTransport link{};
  link.phase = GrinderBlePhase::Subscribe;
  link.handle = 12;
  assert(link.current(0, 12));
  assert(!link.current(0, 13));
  assert(!link.queueCommand("ON\n", 3, 1));
  const uint8_t busy[] = {0, 'B', 'U', 'S', 'Y', '\n'};
  assert(link.queueReply(busy, sizeof(busy)));
  uint8_t id = 255;
  char output[19];
  assert(link.readReply(&id, output, sizeof(output)) == 0);
  link.phase = GrinderBlePhase::Ready;
  assert(link.queueCommand("HELLO 01:2B:30:45:FE:00\n", 24, 1));
  assert(link.readReply(&id, output, sizeof(output)) == 5);
  assert(id == 0 && memcmp(output, "BUSY\n", 5) == 0);
  assert(!link.queueCommand("PING\n", 5, 2));
  assert(link.queueCommand("!", 1, 3));
  assert(link.command.id == 3 && link.command.size == 1 && link.command.bytes[0] == '!');
  assert(!link.queueCommand("!", 1, 0));
  const uint8_t invalid[] = {3, 'O', 0, '\n'};
  assert(!link.queueReply(invalid, sizeof(invalid)));
  for (uint8_t i = 1; i <= grinderBleReplyLimit; ++i) {
    const uint8_t reply[] = {i, 'O', 'K', '\n'};
    assert(link.queueReply(reply, sizeof(reply)));
  }
  assert(!link.queueReply(busy, sizeof(busy)));
  for (uint8_t i = 1; i <= grinderBleReplyLimit; ++i) {
    assert(link.readReply(&id, output, sizeof(output)) == 3);
    assert(id == i && memcmp(output, "OK\n", 3) == 0);
  }
  assert(link.queueReply(busy, sizeof(busy)));
  link.closeRequested = true;
  assert(link.readReply(&id, output, sizeof(output)) == 0);
  assert(!link.queueCommand("ON\n", 3, 4));
  const auto emergency = link.takeCommand();
  assert(emergency.id == 3 && emergency.size == 1 && emergency.bytes[0] == '!');
  assert(link.takeCommand().size == 0);
  link.reset();
  link.phase = GrinderBlePhase::Ready;
  link.handle = 12;
  assert(!link.current(0, 12));
  assert(link.readReply(&id, output, sizeof(output)) == 0);
  assert(link.queueCommand("ON\n", 3, 1));
  link.closeRequested = true;
  assert(link.takeCommand().size == 0);
}
'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        cpp, executable = path / "test.cpp", path / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"),
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    test_grinder_ble_transport()
    print("grinder BLE transport tests passed")
