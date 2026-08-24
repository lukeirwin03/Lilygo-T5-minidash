#include "graphics/sprite.h"
#include <GxEPD2_BW.h>

extern GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display;

namespace sprite {

void draw(const Sprite& s, int x, int y, int scale) {
  if (scale < 1) scale = 1;
  // Each row uses ceil(width / 8) bytes
  int rowBytes = (s.width + 7) / 8;

  for (int py = 0; py < s.height; py++) {
    for (int px = 0; px < s.width; px++) {
      int byteIdx = py * rowBytes + (px / 8);
      int bitIdx  = 7 - (px % 8);
      bool on = pgm_read_byte(&s.data[byteIdx]) & (1 << bitIdx);
      if (on) {
        if (scale == 1) {
          display.drawPixel(x + px, y + py, GxEPD_BLACK);
        } else {
          display.fillRect(x + px * scale, y + py * scale,
                           scale, scale, GxEPD_BLACK);
        }
      }
    }
  }
}

void getBounds(const Sprite& s, int x, int y, int scale,
               int& outX, int& outY, int& outW, int& outH) {
  if (scale < 1) scale = 1;
  outX = x;
  outY = y;
  outW = s.width  * scale;
  outH = s.height * scale;
}

} // namespace sprite