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
#include <array>
#include <cstring>
#include <initializer_list>
#include "app_prompt_state.h"
#include "app_prompt_scroll.h"
#include "u8g2.h"

uint8_t noIo(u8x8_t *, uint8_t, uint8_t, void *) { return 1; }

bool pixelAt(u8g2_t &display, bool flipped, int x, int y) {
  const auto buffer = u8g2_GetBufferPtr(&display);
  const int px = flipped ? 127 - x : x;
  const int py = flipped ? 63 - y : y;
  return buffer[px + (py / 8) * 128] & (1 << (py % 8));
}

int inkBottom(u8g2_t &display, bool flipped) {
  int bottom = -1;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      if (pixelAt(display, flipped, x, y)) bottom = y;
    }
  }
  return bottom;
}

std::array<uint8_t, 1024> promptPixels(u8g2_t &display, const char *text, int x) {
  u8g2_ClearBuffer(&display);
  u8g2_DrawStr(&display, x, 48, text);
  std::array<uint8_t, 1024> result;
  std::memcpy(result.data(), u8g2_GetBufferPtr(&display), result.size());
  return result;
}

void checkPromptPixels(u8g2_t &display, bool flipped, bool expectInk) {
  bool hasInk = false;
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 128; ++x) {
      if (!pixelAt(display, flipped, x, y)) continue;
      hasInk = true;
      assert(y >= 40 && y < 51);
    }
  }
  assert(!expectInk || hasInk);
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
    const auto scroll = appPromptScrollUpdate(AppPromptScrollState{}, true, 1, 0);
    for (const size_t length : {18U, 21U, 22U, 64U, 128U}) {
      for (int character = 32; character <= 126; ++character) {
        char text[APP_PROMPT_MAX_BYTES + 1] = {};
        std::memset(text, character, length);
        const int textWidth = u8g2_GetStrWidth(&display, text);
        assert(textWidth > 0);
        const int overflow = textWidth > width ? textWidth - width : 0;
        const unsigned long endAt = 1000UL + overflow * 100UL;
        for (const unsigned long now : {0UL, 999UL, 1100UL, endAt, endAt + 999UL, endAt + 1000UL}) {
          const int x = appPromptTextX(scroll, textWidth, width, now);
          if (overflow == 0) assert(x == (width - textWidth) / 2);
          else assert(x <= 0 && x >= -overflow);
          promptPixels(display, text, x);
          checkPromptPixels(display, rotation == U8G2_R2, character != 32);
        }
      }
    }
    const char *text = "Carpano Antica Sweet Vermouth: 31g";
    const int textWidth = u8g2_GetStrWidth(&display, text);
    const unsigned long endAt = 1000UL + (textWidth - width) * 100UL;
    const auto first = promptPixels(display, text, appPromptTextX(scroll, textWidth, width, 0));
    assert(first == promptPixels(display, text, appPromptTextX(scroll, textWidth, width, 999)));
    const auto moved = promptPixels(display, text, appPromptTextX(scroll, textWidth, width, 1600));
    assert(first != moved);
    const auto last = promptPixels(display, text, appPromptTextX(scroll, textWidth, width, endAt));
    assert(last != first && last != moved);
    assert(last == promptPixels(display, text, appPromptTextX(scroll, textWidth, width, endAt + 999)));
    assert(first == promptPixels(display, text, appPromptTextX(scroll, textWidth, width, endAt + 1000)));
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
    subprocess.run(["g++", "-Os", "-DU8G2_16BIT=", "-I", str(ROOT / "include"), "-I", str(library), str(source), "-x", "c",
                    *(str(path) for path in sources), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("App Text scrolling holds/repeat, clipping, nonblank pixels, weight/status separation, and both rotations passed")
