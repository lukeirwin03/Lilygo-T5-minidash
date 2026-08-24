#pragma once
#include <Arduino.h>

namespace sprite {

// A 1-bit packed bitmap. Width must be a multiple of 8 for clean packing,
// or pad on the right with zero bits.
struct Sprite {
  const uint8_t* data;     // PROGMEM byte array
  uint16_t width;          // pixels
  uint16_t height;         // pixels
};

// Draws a sprite at (x,y) with optional integer scaling (1 = native size).
void draw(const Sprite& s, int x, int y, int scale = 1);

// Computes a partial-window rectangle that just covers the sprite at
// the given position and scale. Useful for animation: you can pad these
// values and pass them to display.setPartialWindow().
void getBounds(const Sprite& s, int x, int y, int scale,
               int& outX, int& outY, int& outW, int& outH);

} // namespace sprite