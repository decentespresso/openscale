from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
library = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / ".pio.nosync/libdeps/esp32s3/U8g2/src/clib"
sketch = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
assert "drawWeightAt(f_displayedValue, LCDHeight - 30);" in sketch
assert "LCDHeight - 16, promptFrame.text" in sketch
program = r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
#include "u8g2.h"

uint8_t noIo(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }

int inkBottom(u8g2_t &display, bool flipped) {
  int bottom = -1;
  const auto buffer = u8g2_GetBufferPtr(&display);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      const int px = flipped ? 127 - x : x;
      const int py = flipped ? 63 - y : y;
      if (buffer[px + (py / 8) * 128] & (1 << (py % 8))) bottom = y;
    }
  }
  return bottom;
}

int main() {
  u8g2_t display;
  for (const auto rotation : {U8G2_R0, U8G2_R2}) {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&display, rotation, noIo, noIo);
    const int width = u8g2_GetDisplayWidth(&display);
    const int height = u8g2_GetDisplayHeight(&display);
    assert(width == 128 && height == 64);
    u8g2_SetFont(&display, u8g2_font_logisoso32_tf);
    assert(height - 30 - u8g2_GetAscent(&display) >= 0);
    u8g2_ClearBuffer(&display);
    for (const char *glyph = "0123456789.-"; *glyph; ++glyph) {
      u8g2_DrawGlyph(&display, 0, height - 30, *glyph);
    }
    u8g2_SetFont(&display, u8g2_font_luBS18_tr);
    u8g2_DrawStr(&display, 0, height - 35, "g");
    const int weightBottom = inkBottom(display, rotation == U8G2_R2);
    assert(weightBottom >= 0);
    u8g2_SetFont(&display, u8g2_font_6x10_tr);
    assert(height - 16 - u8g2_GetAscent(&display) > weightBottom);
    assert(height - 16 - u8g2_GetDescent(&display) < 51);
    for (int character = 32; character <= 126; ++character) {
      char text[19];
      std::memset(text, character, 18);
      text[18] = 0;
      const int textWidth = u8g2_GetStrWidth(&display, text);
      assert(textWidth > 0 && textWidth <= width);
      u8g2_ClearBuffer(&display);
      u8g2_DrawStr(&display, (width - textWidth) / 2, height - 16, text);
      unsigned pixels = 0;
      for (unsigned index = 0; index < 1024; ++index) pixels += u8g2_GetBufferPtr(&display)[index];
      assert(character == 32 || pixels > 0);
    }
  }
}
'''
sources = sorted(library.glob("*.c"))
if not sources or not (library / "u8g2.h").is_file():
    raise SystemExit("Build esp32s3 first or supply the U8g2 src/clib path")
with tempfile.TemporaryDirectory(prefix="app-text-layout-") as temporary:
    source = Path(temporary) / "layout.cpp"
    executable = Path(temporary) / "layout.exe"
    source.write_text(program, encoding="utf-8")
    subprocess.run(["g++", "-Os", "-I", str(library), str(source), "-x", "c",
                    *(str(path) for path in sources), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("App Text font fit, nonblank pixels, weight/status separation, and both rotations passed")
