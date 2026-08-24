#include "display_manager.h"
#include "config.h"
#include "dashboard.h"
#include "graphics/sprite.h"
#include "graphics/icons.h"
#include "fonts/DSDIGI14pt7b.h"
#include "fonts/DSDIGI32pt7b.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <esp_attr.h>

// The display object is global because GxEPD2 expects a single instance.
GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
  GxEPD2_213_BN(
    config::EPD_CS_PIN,
    config::EPD_DC_PIN,
    config::EPD_RST_PIN,
    config::EPD_BUSY_PIN
  )
);

// These are defined in main.cpp and shared via extern.
extern Dashboard* const dashboards[];
extern const size_t NUM_DASHBOARDS;

// These need to survive deep sleep so a button wake remembers which view
// is currently showing and we still do periodic full refreshes.
RTC_DATA_ATTR static int          rtcCurrentDashboard = 0;
RTC_DATA_ATTR static unsigned long rtcPartialCount    = 0;

namespace display_mgr {

  void begin() {
    display.init(115200);
    // Rotation follows the device mounting (1 = right-hand, 3 = left).
    display.setRotation(config::DISPLAY_ROTATION);
    Serial.printf("[display] rotation %d (%s-hand device)\n",
                  config::DISPLAY_ROTATION,
                  config::DISPLAY_ROTATION == 3 ? "left" : "right");
    display.setTextColor(GxEPD_BLACK);
    // Do NOT clearScreen() here — the panel retains its previous image
    // across deep sleep, and clearing on every wake would cause a flash.
    // A fresh splash() or render*() call will overwrite whatever's there.
  }

  void renderCurrentFull() {
    display.setFullWindow();
    display.firstPage();
    do { dashboards[rtcCurrentDashboard]->render(); } while (display.nextPage());
    rtcPartialCount = 0;
    Serial.printf("[display] Full refresh: %s\n",
                  dashboards[rtcCurrentDashboard]->name());
  }

  void renderCurrentPartial() {
    if (rtcPartialCount >= config::FULL_REFRESH_EVERY) {
      renderCurrentFull();
      return;
    }
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do { dashboards[rtcCurrentDashboard]->render(); } while (display.nextPage());
    rtcPartialCount++;
  }

  void switchTo(int newIndex) {
    if (newIndex < 0)                            newIndex = (int)NUM_DASHBOARDS - 1;
    if (newIndex >= (int)NUM_DASHBOARDS)         newIndex = 0;
    rtcCurrentDashboard = newIndex;
    Serial.printf("[display] Switching to: %s\n",
                  dashboards[rtcCurrentDashboard]->name());
    // Partial refresh on switch (snappier UI). FULL_REFRESH_EVERY still
    // forces a full refresh periodically inside renderCurrentPartial().
    renderCurrentPartial();
  }

  // ---- Abbreviation legend (shown while the button is held) ----
  static void drawLegend() {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // Title
    display.setFont(&FreeSansBold9pt7b);
    const char *title = "ABBREVIATIONS";
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(title, 0, 0, &bx, &by, &bw, &bh);
    display.setCursor((display.width() - bw) / 2, 13);
    display.print(title);
    display.drawLine(0, 16, display.width(), 16, GxEPD_BLACK);

    // Two columns of entries (NULL 6x8 font, 10 px line height)
    display.setFont(NULL);
    display.setTextSize(1);

    const int leftX  = 4;
    const int rightX = 130;
    int y = 21;

    // Left column — "now"-view abbreviations
    display.setCursor(leftX, y      ); display.print("feels  feels-like");
    display.setCursor(leftX, y + 10 ); display.print("UV     uv index");
    display.setCursor(leftX, y + 20 ); display.print("humid  humidity %");
    display.setCursor(leftX, y + 30 ); display.print("cloud  cloud cover");
    display.setCursor(leftX, y + 40 ); display.print("rain   precip prob");
    display.setCursor(leftX, y + 50 ); display.print("snow   snow in/%");
    display.setCursor(leftX, y + 60 ); display.print("gust   wind gust");
    display.setCursor(leftX, y + 70 ); display.print("dew    dew point F");

    // Right column — forecast view + symbols
    display.setCursor(rightX, y      ); display.print("WNW    wind compass");
    display.setCursor(rightX, y + 10 ); display.print("vis    visibility mi");
    display.setCursor(rightX, y + 20 ); display.print("+NH    hours ahead");
    display.setCursor(rightX, y + 30 ); display.print("W##    fcst wind mph");
    display.setCursor(rightX, y + 40 ); display.print("U#     fcst UV");
    display.setCursor(rightX, y + 50 ); display.print("C##    fcst cloud %");

    // Hi/lo row with triangle glyphs
    {
      int gy = y + 60;
      display.fillTriangle(rightX + 3,  gy + 1,
                           rightX,      gy + 6,
                           rightX + 5,  gy + 6,
                           GxEPD_BLACK);
      display.fillTriangle(rightX + 10, gy + 1,
                           rightX + 15, gy + 1,
                           rightX + 12, gy + 6,
                           GxEPD_BLACK);
      display.setCursor(rightX + 22, gy + 1);
      display.print("hi / lo");
    }

    // Sunrise/sunset row with sun glyphs
    {
      int gy = y + 70;
      sprite::draw(icons::SUN_RISING,  rightX,      gy - 1, 1);
      sprite::draw(icons::SUN_SETTING, rightX + 10, gy - 1, 1);
      display.setCursor(rightX + 22, gy + 1);
      display.print("rise / set");
    }

    // Footer hint
    display.setCursor(4, display.height() - 8);
    display.print("(release button to return)");
  }

  void renderLegend() {
    if (rtcPartialCount >= config::FULL_REFRESH_EVERY) {
      // Same fall-through behavior as partial — let counter recycle.
      display.setFullWindow();
      display.firstPage();
      do { drawLegend(); } while (display.nextPage());
      rtcPartialCount = 0;
    } else {
      display.setPartialWindow(0, 0, display.width(), display.height());
      display.firstPage();
      do { drawLegend(); } while (display.nextPage());
      rtcPartialCount++;
    }
    Serial.println("[display] Legend shown");
  }

  // Draw `text` centered at cx in the CURRENTLY-SET GFX font with
  // `tracking` px of extra gap between glyphs (letter-spacing — makes the
  // DS-DIGI digital face read as a deliberate boot-screen look).
  // baselineY is the BASELINE. If the tracked width overflows maxWidth,
  // tracking collapses to 0 rather than truncating. Spaces have no ink, so
  // getTextBounds can't see them; their advance is measured once via the
  // "0 0" vs "00" ink-span difference and added to the width/cursor math.
  static void drawTrackedCentered(const char *text, int cx, int baselineY,
                                  int tracking, int maxWidth)
  {
    int16_t q0x, q0y; uint16_t q0w, q0h;
    display.getTextBounds("00", 0, 0, &q0x, &q0y, &q0w, &q0h);
    int16_t q1x, q1y; uint16_t q1w, q1h;
    display.getTextBounds("0 0", 0, 0, &q1x, &q1y, &q1w, &q1h);
    const int spaceAdv = (int)q1w - (int)q0w;

    // Pass 1: total tracked width = sum of per-char ink widths + space
    // advances + tracking between every adjacent pair (n-1 gaps).
    auto measure = [&](int gap) {
      int w = 0;
      bool first = true;
      for (const char *p = text; *p; p++) {
        if (!first) w += gap;
        first = false;
        if (*p == ' ') { w += spaceAdv; continue; }
        char one[2] = { *p, '\0' };
        int16_t bx, by; uint16_t bw, bh;
        display.getTextBounds(one, 0, 0, &bx, &by, &bw, &bh);
        w += (int)bw;
      }
      return w;
    };
    int totalW = measure(tracking);
    if (totalW > maxWidth) {
      tracking = 0;
      totalW = measure(0);
    }

    // Pass 2: draw char by char. Each glyph's cursor shifts by its own bx
    // ink offset so the INK (not the advance box) lands where it was
    // measured (same idea as weather_dashboard.cpp's drawTextLeftAlign).
    // Cursor y is the BASELINE.
    int x = cx - totalW / 2;
    for (const char *p = text; *p; p++) {
      if (*p == ' ') { x += spaceAdv + tracking; continue; }
      char one[2] = { *p, '\0' };
      int16_t bx, by; uint16_t bw, bh;
      display.getTextBounds(one, 0, 0, &bx, &by, &bw, &bh);
      display.setCursor(x - (int)bx, baselineY);
      display.print(one);
      x += (int)bw + tracking;
    }
  }

  // Copy `src` into `dst` capped at `cap` chars; a longer string keeps its
  // first cap-3 chars and ends "..." so builtin-font stat lines (6 px/char)
  // can never run into the mark column at x=228 — see drawBootScreen for
  // the per-row arithmetic.
  static void truncAscii(const char *src, char *dst, size_t cap) {
    size_t n = strlen(src);
    if (n > cap) {
      memcpy(dst, src, cap - 3);
      dst[cap - 3] = '.'; dst[cap - 2] = '.'; dst[cap - 1] = '.';
      dst[cap] = '\0';
    } else {
      strcpy(dst, src);
    }
  }

  // 8×8 state mark for the boot screen's stat rows: hollow box = pending,
  // ✓ = ok, ✗ = fail. (x, y) is the box's top-left; ink spans x..x+7,
  // same 8-px band as the builtin-font row it marks.
  static void drawBootMark(BootMark m, int x, int y) {
    switch (m) {
      case BOOT_OK:
        display.drawLine(x,     y + 4, x + 2, y + 6, GxEPD_BLACK);  // short down
        display.drawLine(x + 2, y + 6, x + 7, y,     GxEPD_BLACK);  // long up
        break;
      case BOOT_FAIL:
        display.drawLine(x,     y, x + 6, y + 6, GxEPD_BLACK);
        display.drawLine(x + 6, y, x,     y + 6, GxEPD_BLACK);
        break;
      case BOOT_PENDING:
      default:
        display.drawRect(x, y, 7, 7, GxEPD_BLACK);
        break;
    }
  }

  // ---- Boot / status screen ----
  //
  // Shown on cold boot (staged: initial draw, then a redraw after WiFi
  // comes up, another after MQTT — each with the marks/bar reflecting what
  // has actually happened) and on a button wake with no cached payload.
  // E-paper can't animate, so "progress" is real: callers redraw at actual
  // milestones and this function just renders the passed state. Replaces
  // the old splash screen and its "T5" wordmark — the boot branding is now
  // a sun icon + "WEATHER" (callers: main.cpp's handleScheduledWake cold
  // path and handleButtonWake's no-data path).
  void drawBootScreen(BootMark wifi, BootMark mqtt, const char* note) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      display.setTextColor(GxEPD_BLACK);

      // -- Wordmark: SUN_BIG icon + 32pt "WEATHER", centered as a unit ---
      // 32pt letters/digits are 39-40 px tall with yOffset -39 (ascent 39)
      // → ink top y=6 puts the baseline at y=45. "WEATHER" ink is 210 px
      // (6 × 31 xAdvance + 24 for the final 'R'), so the preferred 10-px
      // icon→text gap would make the unit 32+10+210 = 252 > the 250-px
      // screen — the gap auto-shrinks to 8 so the unit fits edge to edge
      // with nothing clipped. The icon is vertically centered against the
      // text band (top y = 6 + (textH-32)/2). Cursor y is the BASELINE.
      display.setFont(&DSDIGI32pt7b);
      const char *wordmark = "WEATHER";
      int16_t wbx, wby; uint16_t wbw, wbh;
      display.getTextBounds(wordmark, 0, 0, &wbx, &wby, &wbw, &wbh);
      const int textH = (wbh > 0) ? (int)wbh : 40;   // tallest glyph ink height
      int gap = 10;                                  // icon -> text ink
      if (32 + gap + (int)wbw > display.width())
        gap = display.width() - 32 - (int)wbw;
      const int unitW = 32 + gap + (int)wbw;
      const int unitLeft = (display.width() - unitW) / 2;
      sprite::draw(icons::SUN_BIG, unitLeft, 6 + (textH - 32) / 2, 1);
      display.setCursor(unitLeft + 32 + gap - (int)wbx, 6 + 39);
      display.print(wordmark);

      // -- Subtitle: tracked 14pt (caps 17 tall / yOffset -16 → baseline
      // 64 puts the ink at y=48..64, clear of the wordmark above and the
      // separator below). Carries the hand marker (L/R) so a desk full
      // of devices can be told apart at a glance on cold boot.
      char sub[24];
      snprintf(sub, sizeof(sub), "MQTT DASHBOARD %s",
               config::DISPLAY_ROTATION == 3 ? "L" : "R");
      display.setFont(&DSDIGI14pt7b);
      drawTrackedCentered(sub, display.width() / 2, 64, 3,
                          display.width() - 10);

      display.drawLine(0, 70, display.width(), 70, GxEPD_BLACK);

      // -- Stats rows: builtin 6x8 font (cursor y is the glyph TOP) ------
      // Rows at y=76/88/100 (ink ~8 px tall). Value caps keep every line
      // clear of the mark column at x=228: builtin advance is 6 px/char,
      // label "wifi  "/"mqtt  " is 6 chars (36 px) from x=4, so a wifi
      // value ≤22 chars ends at 4+36+22·6=172, an mqtt value ≤24 chars
      // ends at 184, and the 28-char note row ends at 4+28·6=172 — all
      // well under 228.
      display.setFont(NULL);
      display.setTextSize(1);
      char vbuf[32];
      const int row1Y = 76, row2Y = 88, row3Y = 100;

      display.setCursor(4, row1Y);
      display.print("wifi  ");
      truncAscii(config::WIFI_SSID, vbuf, 22);
      display.print(vbuf);

      display.setCursor(4, row2Y);
      display.print("mqtt  ");
      {
        char hp[40];
        snprintf(hp, sizeof(hp), "%s:%d", config::MQTT_HOST, config::MQTT_PORT);
        truncAscii(hp, vbuf, 24);
      }
      display.print(vbuf);

      display.setCursor(4, row3Y);
      truncAscii(note, vbuf, 28);
      display.print(vbuf);

      // -- State marks for rows 1-2 (row 3's note gets none) -------------
      drawBootMark(wifi, 228, row1Y);
      drawBootMark(mqtt, 228, row2Y);

      // -- Progress bar: 3 segments = wifi / mqtt / payload --------------
      // Outer box x=8..241 (drawRect 234 px wide), y=111..119; interior
      // x=9..240 (232 px) = 3 segments of 76 px + two 2-px gaps. One
      // segment fills per BOOT_OK mark, from the left. The third segment
      // (payload) never fills on this screen — the dashboard render takes
      // over the moment data lands, which is the honest signal.
      display.drawRect(8, 111, 234, 9, GxEPD_BLACK);
      static const int segX[3] = {9, 87, 165};
      int okCount = (wifi == BOOT_OK) + (mqtt == BOOT_OK);
      for (int i = 0; i < okCount; i++)
        display.fillRect(segX[i], 112, 76, 7, GxEPD_BLACK);
    } while (display.nextPage());
  }

  int currentIndex()           { return rtcCurrentDashboard; }
}