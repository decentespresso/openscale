import re
import subprocess
import tempfile
from pathlib import Path

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    sketch = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    helper = block_after(menu, "void drawMenuButtonIndicators() {")
    showMenu = block_after(menu, "void showMenu() {")
    page = block_after(showMenu, "do {")
    assert showMenu.count("drawMenuButtonIndicators();") == 1
    assert re.search(r"\}\s*drawMenuButtonIndicators\(\);\s*$", page)
    assert "if (renderMenuInfoPage()) return;" in showMenu
    assert not re.search(r"\b(?:firstPage|nextPage)\s*\(", helper)
    assert "setBitmapMode" not in helper
    assert "drawMenuButtonIndicators" not in sketch
    source = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2, LOW = 0;
constexpr int LCDWidth = 128, LCDHeight = 64;
bool b_screenFlipped = false;
unsigned heldButtons = 0;
int digitalRead(int pin) {
  return heldButtons & (pin == BUTTON_CIRCLE ? 1u : 2u) ? LOW : 1;
}
struct BoxCall {
  int x, y, width, height, color, mode;
};
struct PixelDisplay {
  std::array<uint8_t, 128 * 64> pixels{};
  std::array<BoxCall, 2> calls{};
  unsigned callCount = 0, modeWrites = 0;
  int color = 0, mode = 0;
  int pixelIndex(int x, int y) const {
    const int index = y * 128 + x;
    return b_screenFlipped ? index : 8191 - index;
  }
  void setDrawColor(int value) { color = value; }
  void setBitmapMode(int value) { mode = value; ++modeWrites; }
  void drawBox(int x, int y, int width, int height) {
    assert(callCount < calls.size());
    calls[callCount++] = {x, y, width, height, color, mode};
    assert(x >= 0 && y >= 0 && x + width <= 128 && y + height <= 64);
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        auto &pixel = pixels[pixelIndex(x + column, y + row)];
        pixel = color == 2 ? pixel ^ 1 : color;
      }
    }
  }
} u8g2;
void drawMenuButtonIndicators() { @HELPER@ }
int main() {
  unsigned preservedText = 0, invertedText = 0;
  for (const bool flipped : {false, true}) {
    b_screenFlipped = flipped;
    for (const int initialMode : {0, 1}) {
      for (unsigned held = 0; held < 4; ++held) {
        heldButtons = held;
        u8g2 = {};
        u8g2.mode = initialMode;
        for (unsigned pixel = 0; pixel < u8g2.pixels.size(); ++pixel) {
          u8g2.pixels[pixel] = pixel % 7 == 0 || pixel / 128 % 5 == 0;
        }
        const auto original = u8g2.pixels;
        auto expected = original;
        for (unsigned pixel = 0; pixel < expected.size(); ++pixel) {
          const int x = flipped ? pixel % 128 : 127 - pixel % 128;
          const bool active = ((held & 1) && x < 12) || ((held & 2) && x >= 115 && x < 127);
          expected[pixel] ^= active;
          if (active) invertedText += original[pixel] != 0;
          else preservedText += original[pixel] != 0;
        }
        for (int repetition = 0; repetition < 2; ++repetition) {
          u8g2.pixels = original;
          u8g2.callCount = 0;
          drawMenuButtonIndicators();
          assert(u8g2.color == 1 && u8g2.mode == initialMode && u8g2.modeWrites == 0);
          assert(u8g2.callCount == ((held & 1) != 0) + ((held & 2) != 0));
          unsigned callIndex = 0;
          for (const int x : {0, 115}) {
            if (!(held & (x == 0 ? 1u : 2u))) continue;
            const auto &call = u8g2.calls[callIndex++];
            assert(call.x == x && call.y == 0 && call.width == 12 && call.height == 64);
            assert(call.color == 2 && call.mode == initialMode);
          }
          assert(u8g2.pixels == expected);
          for (int y = 0; y < 64; ++y) {
            const int index = u8g2.pixelIndex(127, y);
            assert(u8g2.pixels[index] == original[index]);
          }
          for (int y = 62; y < 64; ++y) {
            for (int x = 12; x < 115; ++x) {
              const int index = u8g2.pixelIndex(x, y);
              assert(u8g2.pixels[index] == original[index]);
            }
          }
        }
        heldButtons = 0;
        u8g2.pixels = original;
        u8g2.callCount = 0;
        drawMenuButtonIndicators();
        assert(u8g2.pixels == original && u8g2.callCount == 0);
        assert(u8g2.color == 1 && u8g2.mode == initialMode && u8g2.modeWrites == 0);
        u8g2.pixels[u8g2.pixelIndex(40, 40)] = 1;
        u8g2.drawBox(40, 40, 1, 1);
        assert(u8g2.pixels[u8g2.pixelIndex(40, 40)] == 1);
      }
    }
  }
  assert(preservedText > 0 && invertedText > 0);
}
'''
    source = source.replace("@HELPER@", helper)
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "menu_button_indicators.cpp"
        binary = Path(directory) / "menu_button_indicators.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
    print("menu button indicator tests passed (16 rotation/hold/bitmap cases)")


if __name__ == "__main__":
    main()
