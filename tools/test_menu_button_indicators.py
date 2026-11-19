import re
import subprocess
import tempfile
from pathlib import Path

from test_usb_text_actions_contract import block_after


ROOT = Path(__file__).resolve().parents[1]


def main():
    menu = (ROOT / "include/menu.h").read_text(encoding="utf-8")
    sketch = (ROOT / "src/hds.ino").read_text(encoding="utf-8")
    display = (ROOT / "include/display.h").read_text(encoding="utf-8")
    helper = block_after(menu, "void drawMenuButtonIndicators() {")
    buttons = block_after(sketch, "void drawButton() {")
    showMenu = block_after(menu, "void showMenu() {")
    page = block_after(showMenu, "do {")
    assert showMenu.count("drawMenuButtonIndicators();") == 1
    assert re.search(r"\}\s*drawMenuButtonIndicators\(\);\s*$", page)
    assert "if (renderMenuInfoPage()) return;" in showMenu
    assert not re.search(r"\b(?:firstPage|nextPage)\s*\(", helper + buttons)
    images = re.findall(
        r"^static const unsigned char image_(?:circle|square)\[\] = \{[^}]*\};",
        display,
        flags=re.MULTILINE,
    )
    assert len(images) == 2
    source = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
constexpr int BUTTON_CIRCLE = 1, BUTTON_SQUARE = 2, LOW = 0;
bool b_screenFlipped = false;
unsigned heldButtons = 0;
int digitalRead(int pin) {
  return heldButtons & (pin == BUTTON_CIRCLE ? 1u : 2u) ? LOW : 1;
}
@IMAGES@
struct BitmapCall {
  int x, y, width, height, color, mode;
  const unsigned char *bitmap;
};
struct PixelDisplay {
  std::array<uint8_t, 128 * 64> pixels{};
  std::array<BitmapCall, 2> calls{};
  unsigned callCount = 0;
  int color = 0, mode = 0;
  int pixelIndex(int x, int y) const {
    const int index = y * 128 + x;
    return b_screenFlipped ? index : 8191 - index;
  }
  void setDrawColor(int value) { color = value; }
  void setBitmapMode(int value) { mode = value; }
  void drawXBM(int x, int y, int width, int height, const unsigned char *bitmap) {
    assert(callCount < calls.size());
    calls[callCount++] = {x, y, width, height, color, mode, bitmap};
    assert(x >= 0 && y >= 0 && x + width <= 128 && y + height <= 64);
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        const bool bit = bitmap[row * ((width + 7) / 8) + column / 8] & (1u << (column % 8));
        auto &pixel = pixels[pixelIndex(x + column, y + row)];
        if (bit) pixel = color == 2 ? pixel ^ 1 : color;
        else if (!mode) pixel = 0;
      }
    }
  }
} u8g2;
void drawButton() { @BUTTONS@ }
void drawMenuButtonIndicators() { @HELPER@ }
int main() {
  unsigned transparentText = 0, invertedText = 0;
  for (const bool flipped : {false, true}) {
    b_screenFlipped = flipped;
    for (unsigned held = 0; held < 4; ++held) {
      heldButtons = held;
      u8g2 = {};
      for (unsigned pixel = 0; pixel < u8g2.pixels.size(); ++pixel) {
        u8g2.pixels[pixel] = pixel % 7 == 0 || pixel / 128 % 5 == 0;
      }
      const auto original = u8g2.pixels;
      auto expected = original;
      drawMenuButtonIndicators();
      assert(u8g2.color == 1 && u8g2.mode == 0);
      assert(u8g2.callCount == ((held & 1) != 0) + ((held & 2) != 0));
      unsigned callIndex = 0;
      const auto checkBitmap = [&](unsigned button, int x, int width, const unsigned char *bitmap) {
        if (!(held & button)) return;
        const auto &call = u8g2.calls[callIndex++];
        assert(call.x == x && call.y == 0 && call.width == width && call.height == 16);
        assert(call.bitmap == bitmap && call.color == 2 && call.mode == 1);
        for (int y = 0; y < 16; ++y) {
          for (int column = 0; column < width; ++column) {
            const int index = flipped ? y * 128 + x + column : (63 - y) * 128 + 127 - x - column;
            const bool bit = bitmap[y * 2 + column / 8] & (1u << (column % 8));
            if (bit) {
              invertedText += original[index] != 0;
              expected[index] ^= 1;
            } else transparentText += original[index] != 0;
          }
        }
      };
      checkBitmap(1, flipped ? 113 : 0, 15, image_circle);
      checkBitmap(2, flipped ? 0 : 114, 14, image_square);
      assert(u8g2.pixels == expected);
      const unsigned char opaque[] = {0x01};
      u8g2.callCount = 0;
      u8g2.pixels[u8g2.pixelIndex(40, 40)] = 1;
      u8g2.pixels[u8g2.pixelIndex(41, 40)] = 1;
      u8g2.drawXBM(40, 40, 2, 1, opaque);
      assert(u8g2.pixels[u8g2.pixelIndex(40, 40)] == 1);
      assert(u8g2.pixels[u8g2.pixelIndex(41, 40)] == 0);
    }
  }
  assert(transparentText > 0 && invertedText > 0);
}
'''
    source = source.replace("@IMAGES@", "\n".join(images))
    source = source.replace("@BUTTONS@", buttons).replace("@HELPER@", helper)
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "menu_button_indicators.cpp"
        binary = Path(directory) / "menu_button_indicators.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=10)
    print("menu button indicator tests passed (8 rotation/hold cases)")


if __name__ == "__main__":
    main()
