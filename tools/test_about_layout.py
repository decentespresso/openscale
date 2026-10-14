import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from test_custom_build_ota_contract import function_body


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "u8g2Sources", type=Path, nargs="?",
        default=ROOT / ".pio.nosync/libdeps/esp32s3/U8g2/src/clib",
    )
    library = parser.parse_args().u8g2Sources.resolve()
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    sketch = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    display = (ROOT / "include/display.h").read_text(encoding="utf-8")
    menuBody = function_body(menu, "void showAbout() {")
    menuFont = menuBody.split("do {", 1)[1].split("u8g2.drawStr(", 1)[0]
    bleBody = function_body(sketch, "void drawAbout() {")
    bleFont = bleBody[bleBody.index("u8g2.setFont("):bleBody.index("u8g2.setDrawColor(")]
    fontAliases = "\n".join(re.findall(r"^#define FONT_[MS]\s+\w+", display, re.MULTILINE))
    program = r"""
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "u8g2.h"

class Display {
  u8g2_t state = {};
public:
  void setFont(const uint8_t *font) { u8g2_SetFont(&state, font); }
  int getUTF8Width(const char *text) { return u8g2_GetUTF8Width(&state, text); }
} u8g2;

const char *firmwareVersion;
struct Text {
  const char *value;
  const char *c_str() const { return value; }
} actionMessage;
#define FIRMWARE_VER firmwareVersion
#define PCB_VER "PCB: 8.1"
#define LCDWidth 128
#define AC(text) ((LCDWidth - u8g2.getUTF8Width(text)) / 2)
""" + fontAliases + "\nvoid menuFont() {\n" + menuFont + "\n}\nvoid bleFont() {\n" + bleFont + "\n}\n" + r"""
int main() {
  for (const char *version : {"3.1.15", "3.1.14-custom", "3.1.15-main-custom"}) {
    char label[64];
    std::snprintf(label, sizeof(label), "FW: %s", version);
    firmwareVersion = label;
    actionMessage.value = label;
    for (auto selectFont : {menuFont, bleFont}) {
      selectFont();
      const int width = u8g2.getUTF8Width(FIRMWARE_VER);
      std::printf("%s: %s = %d pixels\n", selectFont == menuFont ? "Menu" : "BLE", version, width);
      std::fflush(stdout);
      assert(width > 0 && AC(FIRMWARE_VER) >= 0 && AC(FIRMWARE_VER) + width <= LCDWidth);
      if (selectFont == bleFont) assert(AC(PCB_VER) >= 0);
    }
  }
}
"""
    sources = sorted(library.glob("*.c"))
    if not sources or not (library / "u8g2.h").is_file():
        raise SystemExit("Build esp32s3 first or supply the U8g2 src/clib path")
    with tempfile.TemporaryDirectory(prefix="about-layout-") as temporaryDirectory:
        root = Path(temporaryDirectory)
        source = root / "test.cpp"
        executable = root / "test.exe"
        source.write_text(program, encoding="utf-8")
        subprocess.run([
            "g++", "-Os", "-I", str(library), str(source), "-x", "c",
            *(str(path) for path in sources), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    print("About layout tests passed")


if __name__ == "__main__":
    main()
