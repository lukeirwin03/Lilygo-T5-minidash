#include "dashboards/test_dashboard.h"
#include "config.h"
#include "display_manager.h"
#include "fonts/DSDIGI24pt7b.h"

// ---- Static test pattern -----------------------------------------------------
//
// The flashtool's verification screen: renders with no data and no
// publisher, so a freshly flashed device can be sanity-checked before any
// MQTT infrastructure exists — and, because the pattern is deliberately
// asymmetric (labeled corners + orientation word), a wrong-rotation flash
// is obvious at one glance. Doubles as a no-infrastructure diagnostic
// screen. Subscribing to "dashboard/test" is harmless; a future idea:
// publish text to it for a desk message board.

// Right-align `text` so its rightmost ink pixel lands at rightX, in the
// currently-set font; y follows the current font's convention. Deliberately
// mirrors weather_dashboard.cpp's measure-then-shift pattern so this
// dashboard stays self-contained.
static void drawTextRightAlign(const char *text, int rightX, int y)
{
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  // Last ink pixel sits at cursor_x + bx + bw; shift so it lands at rightX.
  display.setCursor(rightX - (int)bx - (int)bw, y);
  display.print(text);
}

// Center `text` horizontally in the builtin 6x8 font (cursor y = glyph TOP).
static void drawCenteredSmall(const char *text, int y)
{
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  display.setCursor(display.width() / 2 - (int)bw / 2, y);
  display.print(text);
}

void TestDashboard::handlePayload(JsonDocument& doc)
{
  (void)doc;   // static screen — nothing to parse
}

void TestDashboard::render()
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // 1-px border inset 2 px; corners are labeled so flip/rotation mistakes
  // can't hide. All math is width()/height()-relative — the pattern is
  // rotation-agnostic by construction.
  display.drawRect(2, 2, display.width() - 4, display.height() - 4, GxEPD_BLACK);

  display.setFont(NULL);
  display.setTextSize(1);
  // Builtin 6x8 font: cursor y is the glyph TOP (ink rows y..y+7).
  display.setCursor(6, 6);
  display.print("TL");
  drawTextRightAlign("TR", display.width() - 6, 6);
  display.setCursor(6, display.height() - 14);
  display.print("BL");
  drawTextRightAlign("BR", display.width() - 6, display.height() - 14);

  // Orientation word, 24pt DS-DIGI, ink-centered. Caps are 29-31 px tall
  // with ascent 30 → ink top y=40 puts the baseline at y=70. "RIGHT HAND"
  // is ~10 glyphs at 23 px xAdvance ≈ 225 px of ink — fits the 246-px
  // interior; measured at runtime anyway. Cursor y is the BASELINE.
  const char *word = config::DISPLAY_ROTATION == 3 ? "LEFT HAND" : "RIGHT HAND";
  display.setFont(&DSDIGI24pt7b);
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(word, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(display.width() / 2 - (int)bw / 2 - (int)bx, 70);
  display.print(word);

  // Builtin-font captions under the word.
  display.setFont(NULL);
  display.setTextSize(1);
  drawCenteredSmall("static dash -- no data needed", 86);
  drawCenteredSmall("flashtool verification pattern", 98);
}
