#include "dashboards/weather_dashboard.h"
#include "battery.h"
#include "display_manager.h"
#include "power_mgr.h"
#include "graphics/sprite.h"
#include "graphics/icons.h"
#include "fonts/DSDIGI12pt7b.h"
#include "fonts/DSDIGI14pt7b.h"
#include "fonts/DSDIGI18pt7b.h"
#include "fonts/DSDIGI24pt7b.h"
#include "fonts/DSDIGI32pt7b.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "util/pure.h"

// ============================================================================
// Width budgets (FreeMonoBold9pt7b advance ≈ 11 px/char):
//   Now-view right column (78 px)   → 7 chars max for stats
//   Forecast-view per column (~62 px)→ 5 chars max for stats
//   Bottom band half (~120 px)      → 9 chars max
// Digits/times render via Adafruit GFX with the DS-DIGI TrueType font
// (DSDIGI{12,14,18,24,32}pt7b). Negatives use the font's own '-'; times
// print as "H:MMAM"/"H:MMPM" via drawTime12hLeftAlign() (the 14pt call
// sites), while the header mixes 12pt digits with a built-in-font suffix.
//
// CRITICAL: Adafruit GFX positions text by its BASELINE — display.setCursor
// (x, y) puts y at the baseline (bottom of most glyphs), NOT the glyph top.
// Every call site below converts a desired top y to baselineY = topY + ascent.
// ============================================================================

// ---- Shared helpers ---------------------------------------------------------

static void drawCenteredText(const char *text, int cx, int y)
{
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(text, 0, y, &bx, &by, &bw, &bh);
  display.setCursor(cx - bw / 2, y);
  display.print(text);
}

// Prints "label N" in FreeSansBold9pt7b, then a small default-font '%' so the
// percent glyph doesn't overflow the 91-px stats column. Leaves the font set
// back to FreeSansBold9pt7b so callers can continue without re-setting it.
static void printPercent(int x, int y, const char *label, int value)
{
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(x, y);
  display.printf("%s %d", label, value);
  int endX = display.getCursorX();
  display.setFont(NULL);
  display.setTextSize(1);
  // Bold-font y is the baseline; default-font cursor y is the glyph top.
  // y-8 puts a 7-px-tall '%' so its top roughly aligns with the bold caps.
  display.setCursor(endX + 1, y - 8);
  display.print('%');
  display.setFont(&FreeSansBold9pt7b);
}

static const char *uvShort(float uv)
{
  static char buf[10];
  if (uv < 10.0f) snprintf(buf, sizeof(buf), "UV %.0f %s", uv, pure::uvCode(uv));
  else            snprintf(buf, sizeof(buf), "UV %.0f", uv);
  return buf;
}

// ---- DS-DIGI font helpers --------------------------------------------------
//
// DS-DIGI is the TrueType digital-watch face rendered through Adafruit GFX.
// Glyph coverage is the full printable ASCII range 0x20-0x7E (so the 'M' in
// "AM"/"PM" is fine). The degree sign is NOT in the font, so the
// big-temperature degree is drawn as a small circle (drawDegreeGlyph).
// Remember: Adafruit GFX setCursor(x, y) takes the BASELINE as y, not the
// glyph top -- every call site converts top -> baseline.

// Ink width of `text` in the currently-set font (via getTextBounds).
static int textWidth(const char *text)
{
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  return (int)bw;
}

// Ink width of an integer (with leading '-' if negative) in the current font.
static int textNumberWidth(int num)
{
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", num);
  return textWidth(buf);
}

// Left-align `text` so its leftmost ink pixel lands at leftX; baselineY is the
// BASELINE. Returns the rightmost ink x (leftX + ink width).
static int drawTextLeftAlign(const GFXfont &font, const char *text,
                             int leftX, int baselineY)
{
  display.setFont(&font);
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
  // First ink pixel sits at cursor_x + bx; shift cursor so it lands at leftX.
  display.setCursor(leftX - (int)bx, baselineY);
  display.print(text);
  return leftX + (int)bw;
}

// Right-align `num` so its rightmost ink pixel lands at rightX; baselineY is
// the BASELINE. Negatives are handled natively (font has '-').
static void drawNumberRightAlign(const GFXfont &font, int num,
                                 int rightX, int baselineY)
{
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", num);
  display.setFont(&font);
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
  // Last ink pixel sits at cursor_x + bx + bw; shift so it lands at rightX.
  display.setCursor(rightX - (int)bx - (int)bw, baselineY);
  display.print(buf);
}

// Center `num` horizontally at cx; baselineY is the BASELINE. Same measure-
// and-shift idea as drawTextLeftAlign: the ink span starts at cursor_x + bx,
// so the cursor shifts left by bx to keep the INK (not the advance box)
// centered.
static void drawNumberCentered(const GFXfont &font, int num,
                               int cx, int baselineY)
{
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", num);
  display.setFont(&font);
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(cx - (int)bw / 2 - (int)bx, baselineY);
  display.print(buf);
}

// Format hour24:minute as 12-hour "H:MMAM"/"H:MMPM" (no leading zero on the
// hour; minute always two digits; full AM/PM with no space, embedded in the
// string so a single print() renders the whole time; buf[10] comfortably
// holds "12:00AM").
static String formatTime12h(int hour24, int minute)
{
  int h12 = hour24 % 12;
  if (h12 == 0) h12 = 12;
  bool pm = hour24 >= 12;
  char buf[10];
  snprintf(buf, sizeof(buf), "%d:%02d%s", h12, minute, pm ? "PM" : "AM");
  return String(buf);
}

// Split form of formatTime12h() for callers that render the meridiem in a
// different font: fills outTime ("H:MM") and outMer ("AM"/"PM").
static void splitTime12h(int hour24, int minute, char *outTime, size_t timeCap,
                         char *outMer, size_t merCap)
{
  int h12 = hour24 % 12;
  if (h12 == 0) h12 = 12;
  snprintf(outTime, timeCap, "%d:%02d", h12, minute);
  strlcpy(outMer, hour24 >= 12 ? "PM" : "AM", merCap);
}

// Draw "H:MMAM"/"H:MMPM" left-aligned at leftX (ink left at leftX); baselineY
// is the BASELINE. Returns the rightmost ink x.
static int drawTime12hLeftAlign(const GFXfont &font, int hour24, int minute,
                                int leftX, int baselineY)
{
  String s = formatTime12h(hour24, minute);
  return drawTextLeftAlign(font, s.c_str(), leftX, baselineY);
}

// ISO-string variant: parse "YYYY-MM-DDTHH:MM" then draw as above.
static int drawTime12hLeftAlignIso(const GFXfont &font, const String &iso,
                                   int leftX, int baselineY)
{
  int hour = 0, minute = 0;
  if (!pure::parseIsoHourMinute(iso.c_str(), iso.length(), hour, minute)) return leftX;
  return drawTime12hLeftAlign(font, hour, minute, leftX, baselineY);
}

// Small hollow circle for the degree sign -- DS-DIGI has no degree glyph.
// (x, topY) is the upper-left of the circle's box; h is the digit height it
// should scale to. r = max(3, (h+6)/10) -- ~4 for 32pt, ~6 for 48pt.
static void drawDegreeGlyph(int x, int topY, int h)
{
  int r = (h + 6) / 10;
  if (r < 3) r = 3;
  display.drawCircle(x + r, topY + r + 1, r, GxEPD_BLACK);
}

// ---- Payload parsing --------------------------------------------------------

void WeatherDashboard::handlePayload(JsonDocument &doc)
{
  // Reset before parsing so a payload missing hours[] or days[] does not
  // render stale fields from a previous publish.
  forecastCount = 0;
  hasDay = false;

  // Combine separate city/state fields into "City, State". Falls back to
  // the em dash only when both are absent so the header never goes blank.
  {
    String c  = doc["city"]  | String("");
    String st = doc["state"] | String("");
    if (c.length() == 0)       city = "—";
    else if (st.length() == 0) city = c;
    else                       city = c + ", " + st;
  }
  model = doc["model"] | "";
  updatedAt = doc["updated"] | "";

  JsonArray hours = doc["hours"];
  if (!hours.isNull() && hours.size() > 0)
  {
    JsonObject head = hours[0];
    currentTime = head["time"] | "";
    currentTemp = head["temp_f"] | 0.0f;
    feelsLike = head["feels_like_f"] | 0.0f;
    currentCode = head["weather_code"] | -1;
    currentWindMph = head["wind_mph"] | 0.0f;
    currentPrecipPb = head["precip_prob"] | 0;
    currentPrecipIn = head["precip_in"] | 0.0f;
    currentHumidityPct = head["humidity_pct"] | 0;
    currentDewPointF = head["dew_point_f"] | 0.0f;
    currentCloudPct = head["cloud_cover_pct"] | 0;
    currentVisibilityM = head["visibility_m"] | 0.0f;
    currentUvIndex = head["uv_index"] | 0.0f;
    currentWindGustMph = head["wind_gust_mph"] | 0.0f;
    currentWindDirDeg = head["wind_dir_deg"] | 0;
    currentPressureHpa = head["pressure_hpa"] | 0.0f;
    currentSnowIn = head["snow_in"] | 0.0f;
    currentIsDay = (head["is_day"] | 1) != 0;

    for (JsonObject h : hours)
    {
      if (forecastCount >= config::FORECAST_HOURS_SHOWN) break;
      forecast[forecastCount].offset_h = h["offset_h"] | 0;
      forecast[forecastCount].temp_f = h["temp_f"] | 0.0f;
      forecast[forecastCount].feels_like_f = h["feels_like_f"] | 0.0f;
      forecast[forecastCount].precip_prob = h["precip_prob"] | 0;
      forecast[forecastCount].weather_code = h["weather_code"] | -1;
      forecast[forecastCount].wind_mph = h["wind_mph"] | 0.0f;
      forecast[forecastCount].wind_dir_deg = h["wind_dir_deg"] | 0;
      forecast[forecastCount].cloud_cover_pct = h["cloud_cover_pct"] | 0;
      forecast[forecastCount].uv_index = h["uv_index"] | 0.0f;
      forecast[forecastCount].is_day = (h["is_day"] | 1) != 0;
      forecastCount++;
    }
  }

  JsonArray days = doc["days"];
  if (!days.isNull() && days.size() > 0)
  {
    JsonObject d = days[0];
    dayHighF         = d["high_f"]                | 0.0f;
    dayLowF          = d["low_f"]                 | 0.0f;
    daySunrise       = d["sunrise"]               | "";
    daySunset        = d["sunset"]                | "";
    dayWindMax       = d["wind_max_mph"]          | 0.0f;
    dayWindDirDom    = d["wind_dir_dominant_deg"] | 0;
    dayUvMax         = d["uv_index_max"]          | 0.0f;
    dayPrecipProbMax = d["precip_prob_max"]       | 0;
    hasDay = true;
  }

  hasData = true;
  dirty = true;

  Serial.printf("[weather] %s parsed: temp %.1fF code=%d, %d hours, day=%d\n",
                mode == MODE_NOW ? "now" : "forecast",
                currentTemp, currentCode, forecastCount, hasDay ? 1 : 0);
}

// ---- Top-level render dispatch ---------------------------------------------

void WeatherDashboard::render()
{
  display.fillScreen(GxEPD_WHITE);
  if (!hasData) {
    drawWaiting();
    return;
  }
  if (mode == MODE_NOW) renderNow();
  else                  renderForecast();
}

// ---- Header ----------------------------------------------------------------
//
// Layout:
//   [city, ellipsized]  [vld til + DS-DIGI H:MMAM/PM, left-anchored]  ...   [battery meter]
//
// The cluster is anchored at a fixed left x (not centered) so the gap
// between its right edge and the battery meter stays clear — keeps the
// "data freshness" and "device status" zones visually distinct.
//
// The time is hours[0].time + 1h — the data describes hour N, so it stays
// valid until the next hourly publish lands at N+1. "vld til 1:00PM"
// when the data is for 12:00. Wall-clock time would lie by up to 60 min
// since the device deep-sleeps between updates.
//
// Battery level comes from the cached RTC-RAM reading taken at the start
// of setup() (before WiFi TX droops the rails), drawn as a 4-cell meter.

// 4 cells, 20% per cell. -1 (never sampled) renders empty outlines.
static void drawBatteryMeter(int leftX, int topY, int percent)
{
  const int cells = 4;
  const int cellW = 5, cellH = 9;
  const int gap   = 2;

  int filled;
  if      (percent < 0)   filled = 0;
  else if (percent >= 80) filled = 4;
  else if (percent >= 60) filled = 3;
  else if (percent >= 40) filled = 2;
  else if (percent >= 20) filled = 1;
  else                    filled = 0;

  for (int i = 0; i < cells; i++) {
    int x = leftX + i * (cellW + gap);
    display.drawRect(x, topY, cellW, cellH, GxEPD_BLACK);
    if (i < filled) {
      display.fillRect(x, topY, cellW, cellH, GxEPD_BLACK);
    }
  }
}

// Total width occupied by the meter — kept here so the header layout math
// doesn't have to repeat the constants. 4 cells × 5 px + 3 gaps × 2 px.
static constexpr int kBatteryMeterW = 4 * 5 + 3 * 2;   // 26 px

static void drawHeader(const String &city, const String &headlineTimeIso)
{
  display.setTextColor(GxEPD_BLACK);

  const int W = display.width();
  const int rightPad = 4;

  // -- Right-most: 4-cell battery meter (vertically centered with time) ------
  const int batY = 4;                                    // body spans y=4..10
  const int batLeftX = W - rightPad - kBatteryMeterW;
  drawBatteryMeter(batLeftX, batY, battery::lastPercent());

  // -- Left-anchored cluster: "vld til <DS-DIGI H:MMAM/PM>" ------------------
  // Fixed left anchor pulled well right of the city so short names like
  // "Omaha" don't have to ellipsize, while still leaving a visible gap
  // before the battery meter on the right. Label width is measured at
  // render time so a future label tweak keeps the layout consistent.
  const char* labelText = "vld til";
  display.setFont(NULL);
  display.setTextSize(1);
  int16_t lbx, lby; uint16_t lbw, lbh;
  display.getTextBounds(labelText, 0, 0, &lbx, &lby, &lbw, &lbh);
  const int labelW      = (int)lbw;
  const int labelGap    = 3;
  const int clusterLeft = 110;            // fixed anchor — visually balanced
  const int labelX      = clusterLeft;
  const int timeLeftX   = clusterLeft + labelW + labelGap;

  display.setCursor(labelX, 7);
  display.print(labelText);

  // DS-DIGI 12pt digits + built-in-font AM/PM suffix = hours[0].time + 1h
  // (mod 24). 12pt is the smallest DS-DIGI size that renders cleanly — at
  // 10pt the strokes are 1-px thin and the diagonal segments alias — but a
  // full 12pt "12:00AM" (~70 px) overruns the battery meter, so only the
  // "H:MM" part is DS-DIGI and the meridiem prints in the small 6×8 font
  // (the same mixed-font trick printPercent() uses for '%'). 12pt digits
  // are 15 px tall with yOffset -14 → at baseline 15, ink spans y=1..15
  // (the original pre-10pt geometry). The 7-px-tall suffix caps start at
  // top y=5 (ink ~5..12), vertically centered against the digit band.
  // Worst case "12:00"+"AM" ends ~x=215, clear of the battery at x=220.
  // GFX cursor y is BASELINE for the 12pt part, glyph TOP for the suffix.
  int timeRightX = timeLeftX;
  if (headlineTimeIso.length() >= 16) {
    int hour   = (headlineTimeIso[11] - '0') * 10 + (headlineTimeIso[12] - '0');
    int minute = (headlineTimeIso[14] - '0') * 10 + (headlineTimeIso[15] - '0');
    int validUntilHour = (hour + 1) % 24;
    char tbuf[8], mbuf[4];
    splitTime12h(validUntilHour, minute, tbuf, sizeof(tbuf), mbuf, sizeof(mbuf));
    int digitsW = drawTextLeftAlign(DSDIGI12pt7b, tbuf, timeLeftX, 15);
    display.setFont(NULL);
    display.setTextSize(1);
    int16_t mbx, mby; uint16_t mbw, mbh;
    display.getTextBounds(mbuf, 0, 0, &mbx, &mby, &mbw, &mbh);
    display.setCursor(digitsW + 2 - (int)mbx, 5);
    display.print(mbuf);
    timeRightX = digitsW + 2 + (int)mbw;
  } else {
    display.setFont(&DSDIGI12pt7b);
    int fallbackW = textWidth("12:00");
    display.setFont(NULL);
    display.setTextSize(1);
    int16_t mbx, mby; uint16_t mbw, mbh;
    display.getTextBounds("AM", 0, 0, &mbx, &mby, &mbw, &mbh);
    timeRightX = timeLeftX + fallbackW + 2 + (int)mbw;
  }

  // -- Vertical divider between the cluster and the battery -----------------
  // Centered between the time's right ink edge and the battery cells, matched
  // in height to the cells (y=4..12) so it reads as a deliberate separator.
  const int dividerX = (timeRightX + batLeftX) / 2;
  display.drawLine(dividerX, 4, dividerX, 12, GxEPD_BLACK);

  // -- City on the left, ellipsized so it cannot collide with the cluster ----
  display.setFont(&FreeSansBold9pt7b);
  const int locMaxRight = clusterLeft - 6;
  String shown = city;
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(shown, 3, 13, &bx, &by, &bw, &bh);
  if ((int)(3 + bw) > locMaxRight && shown.length() > 1) {
    while (shown.length() > 1) {
      shown.remove(shown.length() - 1);
      String probe = shown + "...";
      display.getTextBounds(probe, 3, 13, &bx, &by, &bw, &bh);
      if ((int)(3 + bw) <= locMaxRight) { shown = probe; break; }
    }
  }
  display.setCursor(3, 13);
  display.print(shown);

  // Thin separator below the header.
  display.drawLine(0, 16, W, 16, GxEPD_BLACK);
}

// ---- "Now" view ------------------------------------------------------------

void WeatherDashboard::renderNow()
{
  drawHeader(city, currentTime);

  // Column layout (vert sep at x=155 gives the stats column more room and
  // pulls the temp + hi/lo block closer to the icon):
  //   icon       :  4–35      (32 wide)
  //   temp area  :  40–155    (115 wide, big temp + hi/lo row)
  //   vert sep   :  155
  //   stats      :  157–248   (91 wide)

  // ---- Left: 32x32 weather icon, aligned with the top of the temp digits ----
  const sprite::Sprite &icon = icons::forWeatherCodeBig(currentCode);
  sprite::draw(icon, 4, 22, 1);

  // ---- Center: big temp at top, hi/lo row below ----
  // DS-DIGI 32pt for the big temp. The old segment renderer used dH=36 (and 28 for
  // 3-digit/negative). DS-DIGI's pixel height is ~1.25x its point size, so:
  //   - 48pt = 60 px tall -> overflows the hi/lo row below (only ~44 px of
  //     vertical space before y=64). Not usable here.
  //   - 32pt = 40 px tall -> fits with a 2 px gap to the hi/lo row. Used for
  //     ALL temps (1-, 2-, or 3-digit); 32pt's ink is ~24 px/digit so even
  //     "100" + degree + F fits the 115-px temp area (measured, not branched).
  // The "F" uses 24pt (a unit letter reads best slightly smaller than the
  // number); the degree sign is a drawn circle (DS-DIGI has no degree glyph).
  int tempInt = (int)(currentTemp + 0.5f);
  const GFXfont &numFont = DSDIGI32pt7b;
  const GFXfont &fFont   = DSDIGI24pt7b;

  display.setFont(&numFont);
  const int numW   = textNumberWidth(tempInt);   // ink width of the number
  const int degH   = 40;                         // 32pt digit height
  const int degR   = (degH + 6) / 10;            // MUST match drawDegreeGlyph
  const int degGap = 3;                           // number -> degree circle
  const int fGap   = 3;                           // degree circle -> F
  display.setFont(&fFont);
  const int fW     = textWidth("F");

  const int blockW  = numW + degGap + degR * 2 + fGap + fW;
  const int tempArea = 155 - 40;                  // 115 px wide
  int blockX = 40 + (tempArea - blockW) / 2;
  if (blockX < 40) blockX = 40;
  const int numRightX = blockX + numW;            // ink right edge of number

  // 32pt ascent ~= 39; top y=22 -> baseline y=61. Cursor y is the BASELINE.
  const int tempTopY      = 22;
  const int tempBaselineY = tempTopY + 39;

  drawNumberRightAlign(numFont, tempInt, numRightX, tempBaselineY);
  // Degree circle at the upper-right of the digits.
  drawDegreeGlyph(numRightX + degGap, tempTopY, degH);
  // "F" baseline-aligned with the number, just right of the degree circle.
  drawTextLeftAlign(fFont, "F",
                    numRightX + degGap + degR * 2 + fGap, tempBaselineY);

  // ---- Hi/Lo: horizontal row below the big temp ----
  // DS-DIGI 18pt (digits 22 px tall, ~17 px xAdvance) + the 8x8 up/down
  // triangles. The pair is centered AS A UNIT inside x=4..152 rather than at
  // fixed centers: even at 18pt, digits are wide enough that fixed
  // cx=65/cx=130 collide for 2-digit values, so the combined width is
  // measured and centered. x=4..36 is free at this y-row (the 32x32 icon
  // sits higher up), giving the row the full 4..152. The shorter row is
  // vertically centered in the old y=64..94 band (big-temp ink ends ~y=62,
  // sunrise-band separator at y=96): top y=68, ink y=68..89.
  if (hasDay) {
    const int triW   = 8;
    const int triGap = 3;
    const int digitH = 22;                        // 18pt digit ink height
    const int rowTopY      = 64 + (31 - digitH) / 2;   // center in old 31-px band
    const int rowBaselineY = rowTopY + 21;        // 18pt ascent = 21 (BASELINE)
    const int pairGap = 8;
    const int hiLoLeft  = 4;
    const int hiLoRight = 152;                    // 3 px clear of x=155 sep

    int hi = (int)(dayHighF + 0.5f);
    int lo = (int)(dayLowF + 0.5f);
    display.setFont(&DSDIGI18pt7b);
    int hiElemW = triW + triGap + textNumberWidth(hi);
    int loElemW = triW + triGap + textNumberWidth(lo);
    int pairW   = hiElemW + pairGap + loElemW;
    int pairStart = hiLoLeft + (hiLoRight - hiLoLeft - pairW) / 2;
    if (pairStart < hiLoLeft) pairStart = hiLoLeft;

    auto drawHiLo = [&](int value, int elemLeftX, bool isHi) {
      int triX = elemLeftX;
      int triY = rowTopY + (digitH - triW) / 2;   // center 8-px tri in 22-px digit
      int numRightEdge = elemLeftX + triW + triGap + textNumberWidth(value);
      if (isHi) {
        display.fillTriangle(triX + 4, triY,
                             triX,     triY + 7,
                             triX + 7, triY + 7,
                             GxEPD_BLACK);
      } else {
        display.fillTriangle(triX,     triY,
                             triX + 7, triY,
                             triX + 4, triY + 7,
                             GxEPD_BLACK);
      }
      drawNumberRightAlign(DSDIGI18pt7b, value, numRightEdge, rowBaselineY);
    };

    drawHiLo(hi, pairStart, true);
    drawHiLo(lo, pairStart + hiElemW + pairGap, false);
  }

  // ---- Vertical separator between temp/symbol and stats ----
  display.drawLine(155, 20, 155, 95, GxEPD_BLACK);

  // ---- Right column: profile-driven stats ----
  drawContextStats(157);

  // ---- Bottom band: sunrise + sunset ----
  drawSunriseSunsetFooter();
}

// ---- "Forecast" view -------------------------------------------------------
//
// Vertical budget (122 px tall):
//   y=17..29   hourly column labels "+1H" etc. (FreeSansBold9pt7b)
//   y=31..62   32×32 icons, horizontally centered per column
//   y=65..82   temps (DS-DIGI 14pt), centered per column
//   y=85       separator — hourly section above, daily panel below
//   y=86..110  daily panel: two FreeSansBold9pt7b rows at baselines 98/110
//              (13-px caps/digits put the ink at y=86..98 and 98..110)
//   y=112      separator — footer strip below
//   y=114..121 sleep footer: "zzz sleeping" + next-update time (6×8 font)
// This is the "sleep screen" resting state — the bistable panel keeps the
// final image with zero current draw for the whole deep-sleep interval.

// Returns the forecast entry whose offset_h matches `offset`, or nullptr.
static const ForecastHour* findHourByOffset(const ForecastHour* arr, int count,
                                            int offset) {
  for (int i = 0; i < count; i++) {
    if (arr[i].offset_h == offset) return &arr[i];
  }
  return nullptr;
}

static void drawForecastColumn(const ForecastHour* h, int cx, int colLeft, int colRight)
{
  const int colW = colRight - colLeft;

  // Offset label centered under the header separator. FreeSansBold9pt7b
  // digits/caps are 13 px tall with yOffset -12, so baseline y=29 puts the
  // ink at y=17..29 — 1 px below the y=16 separator and 1 px above the
  // icon band at y=31. Cursor y is the BASELINE.
  char hlabel[6];
  if (h) snprintf(hlabel, sizeof(hlabel), "+%dH", h->offset_h);
  else   snprintf(hlabel, sizeof(hlabel), "--");
  display.setFont(&FreeSansBold9pt7b);
  drawCenteredText(hlabel, cx, 29);

  if (!h) {
    // Built-in 6x8 font, centered in the column. Its cursor y is the glyph
    // TOP (not a baseline), so y=45 places the 8-px text at y=45..52 —
    // roughly the middle of the icon band it stands in for.
    display.setFont(NULL);
    display.setTextSize(1);
    drawCenteredText("no data", cx, 45);
    return;
  }

  // Icon and temp are STACKED: side-by-side exceeds the ~62-px column
  // (32-px icon + 24pt digits alone overflow it), so the 32×32 icon gets
  // its own row, centered (colLeft + (colW-32)/2), occupying y=31..62.
  const int comboL = colLeft + (colW - 32) / 2;
  const sprite::Sprite &ic = icons::forWeatherCodeBig(h->weather_code);
  sprite::draw(ic, comboL, 31, 1);

  // 14pt chosen so 3-digit (100°F ≈ 32 px) and negative (-10 ≈ 29 px) temps
  // both fit the ~62-px column. 14pt ascent ~= 17 ('0' is 18 tall,
  // yOffset -17) → top y=65 means baseline y=82, ink spans y=65..82, clear
  // of the y=85 separator. Cursor y is the BASELINE.
  const int tempInt = (int)(h->temp_f + 0.5f);
  drawNumberCentered(DSDIGI14pt7b, tempInt, cx, 65 + 17);
}

void WeatherDashboard::renderForecast()
{
  drawHeader(city, currentTime);

  // ---- Top section: four hourly columns at +1H / +3H / +5H / +8H ----
  // The publisher sends offsets 1, 2, 3, 5, 8 — these four picks spread the
  // forecast across the day; capacity (FORECAST_HOURS_SHOWN=8) already
  // holds them.
  const ForecastHour* hours[4] = {
    findHourByOffset(forecast, forecastCount, 1),
    findHourByOffset(forecast, forecastCount, 3),
    findHourByOffset(forecast, forecastCount, 5),
    findHourByOffset(forecast, forecastCount, 8),
  };

  const int totalW = display.width();
  const int colW   = totalW / 4;       // 250/4 -> cols of 63, 63, 62, 62
  const int extra  = totalW - colW * 4;

  int x = 0;
  for (int col = 0; col < 4; col++) {
    int thisColW = colW + (col < extra ? 1 : 0);
    int cx = x + thisColW / 2;
    drawForecastColumn(hours[col], cx, x, x + thisColW);

    x += thisColW;
    if (col < 3) {
      // Inter-column separator spans the label + icon + temp band.
      display.drawLine(x, 18, x, 83, GxEPD_BLACK);
    }
  }

  // Separator between the hourly columns and the daily panel.
  display.drawLine(0, 85, display.width(), 85, GxEPD_BLACK);

  // ---- Middle section: daily stats ----
  // Sunrise / sunset moved to the Now-view footer; this panel focuses on
  // hi/lo + max wind and UV max + max rain. Two rows of FreeSansBold9pt7b
  // at a tight 12-px baseline spacing (13-px-tall caps/digits, so the rows
  // just touch at the shared y=98 pixel row) — none of the daily strings
  // have descenders, so the tighter-than-normal spacing is safe.
  display.setFont(&FreeSansBold9pt7b);
  const int leftX  = 2;
  const int rightX = 128;
  const int row1Y  = 98;    // 13-px glyphs ~y=86–98
  const int row2Y  = 110;   // 13-px glyphs ~y=98–110

  if (!hasDay) {
    display.setCursor(4, row1Y);
    display.print("daily summary pending...");
  } else {
    // Row 1: hi/lo  |  max wind (compass + mph)
    display.setCursor(leftX, row1Y);
    display.printf("hi %d  lo %d",
                   (int)(dayHighF + 0.5f), (int)(dayLowF + 0.5f));
    display.setCursor(rightX, row1Y);
    display.printf("wind %s %d",
                   pure::compass8(dayWindDirDom), (int)(dayWindMax + 0.5f));

    // Row 2: UV max  |  max rain probability
    display.setCursor(leftX, row2Y);
    display.printf("UV max %d", (int)(dayUvMax + 0.5f));
    display.setCursor(rightX, row2Y);
    display.printf("rain %d%%", dayPrecipProbMax);
  }

  // ---- Bottom section: sleep footer strip ----
  // The Forecast view is the screen the device sleeps on after a button
  // cycle: the bistable panel keeps this image for the whole deep-sleep
  // interval, so the footer states when fresh data arrives. The next-update
  // time comes from power_mgr::nextWakeTime() — quiet-hours aware, so
  // overnight it shows the 6:00AM-style time of the next wake that will
  // actually happen (wakes inside the quiet range are skipped).
  display.drawLine(0, 112, display.width(), 112, GxEPD_BLACK);
  display.setFont(NULL);
  display.setTextSize(1);
  // Built-in 6x8 font: cursor y is the glyph TOP (y=114 -> ink y=114..121;
  // y=121 is the last screen row — same edge the Now view's bottom border
  // occupies at height-1).
  display.setCursor(4, 114);
  display.print("zzz sleeping");
  struct tm next;
  String right = "next update --:--";
  if (power_mgr::nextWakeTime(&next))
    right = String("next update ") + formatTime12h(next.tm_hour, next.tm_min);
  // Right-align so the ink's right edge lands 4 px from the screen edge —
  // same getTextBounds-measure-then-setCursor pattern as drawCenteredText
  // (built-in font still selected from the "zzz" half above).
  int16_t bx, by;
  uint16_t bw, bh;
  display.getTextBounds(right.c_str(), 0, 114, &bx, &by, &bw, &bh);
  display.setCursor(display.width() - 4 - (int)bw, 114);
  display.print(right);
}

// ---- Hourly profile (right column on renderNow) ----------------------------

WeatherDashboard::Profile WeatherDashboard::detectProfile() const
{
  if (currentWindMph >= 20.0f || currentWindGustMph >= 30.0f)
    return PROF_WINDY;

  int c = currentCode;
  if (c == 95 || c == 96 || c == 99) return PROF_STORM;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86 || currentSnowIn > 0.0f)
    return PROF_SNOW;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82) || currentPrecipIn > 0.0f)
    return PROF_RAIN;
  if (c == 45 || c == 48) return PROF_FOG;
  if (c == 0) return currentIsDay ? PROF_CLEAR_DAY : PROF_CLEAR_NIGHT;
  if (c == 1 || c == 2) return PROF_PARTLY_CLOUDY;
  if (c == 3) return PROF_OVERCAST;
  return PROF_PARTLY_CLOUDY;
}

// Stats column labels follow one convention:
//   - lowercase short word as the prefix ("feels", "humid", "cloud", "rain",
//     "snow", "gust", "wind", "vis", "dew")
//   - % suffix for percentages
//   - " suffix for inches
//   - bare numbers for temperatures (°F implied) and mph (implied)
//   - compass codes (WNW) stay uppercase by meteorological convention
//   - "UV" stays uppercase (acronym)
//
// Each profile picks the three stats most relevant to that condition. No
// stat is shown by default — what you see is what's notable for the
// current weather.
void WeatherDashboard::drawContextStats(int x)
{
  display.setFont(&FreeSansBold9pt7b);
  const int y1 = 32, y2 = 56, y3 = 80;

  // Per-stat renderers — keep the per-profile switch readable.
  auto feels = [&](int y) {
    display.setCursor(x, y);
    display.printf("feels %d", (int)(feelsLike + 0.5f));
  };
  auto uv = [&](int y) {
    display.setCursor(x, y);
    display.print(uvShort(currentUvIndex));
  };
  auto humid    = [&](int y) { printPercent(x, y, "humid", currentHumidityPct); };
  auto cloud    = [&](int y) { printPercent(x, y, "cloud", currentCloudPct); };
  auto rainProb = [&](int y) { printPercent(x, y, "rain",  currentPrecipPb); };
  auto rainIn   = [&](int y) {
    display.setCursor(x, y);
    if (currentPrecipIn < 1.0f) display.printf("rain %.2f\"", currentPrecipIn);
    else                        display.printf("rain %.1f\"", currentPrecipIn);
  };
  auto snowIn = [&](int y) {
    display.setCursor(x, y);
    if (currentSnowIn < 10.0f) display.printf("snow %.1f\"", currentSnowIn);
    else                       display.printf("snow %.0f\"", currentSnowIn);
  };
  auto windCompass = [&](int y) {
    display.setCursor(x, y);
    display.printf("%s %d", pure::compass8(currentWindDirDeg),
                            (int)(currentWindMph + 0.5f));
  };
  auto gust = [&](int y) {
    display.setCursor(x, y);
    display.printf("gust %d", (int)(currentWindGustMph + 0.5f));
  };
  auto vis = [&](int y) {
    float miles = currentVisibilityM / 1609.344f;
    if (miles > 99.0f) miles = 99.0f;
    display.setCursor(x, y);
    if (miles < 10.0f) display.printf("vis %.1f", miles);
    else               display.printf("vis %.0f", miles);
  };
  auto dewPt = [&](int y) {
    display.setCursor(x, y);
    display.printf("dew %d", (int)(currentDewPointF + 0.5f));
  };

  switch (detectProfile())
  {
  case PROF_WINDY:
    windCompass(y1); gust(y2); feels(y3);
    break;
  case PROF_STORM:
    rainIn(y1); gust(y2); rainProb(y3);
    break;
  case PROF_SNOW:
    snowIn(y1); cloud(y2); feels(y3);
    break;
  case PROF_RAIN:
    rainIn(y1); cloud(y2); rainProb(y3);
    break;
  case PROF_FOG:
    vis(y1); humid(y2); feels(y3);
    break;
  case PROF_CLEAR_DAY:
    uv(y1); feels(y2); humid(y3);
    break;
  case PROF_CLEAR_NIGHT:
    feels(y1); humid(y2); dewPt(y3);
    break;
  case PROF_PARTLY_CLOUDY:
    if (currentIsDay) { uv(y1);    cloud(y2); feels(y3); }
    else              { feels(y1); cloud(y2); humid(y3); }
    break;
  case PROF_OVERCAST:
    cloud(y1); feels(y2); rainProb(y3);
    break;
  }
}

// ---- Bottom band: sunrise + sunset -----------------------------------------
//
// Sun-rising glyph + DS-DIGI time on the left half, sun-setting on the right.
// Horizontal separators bracket the band.

void WeatherDashboard::drawSunriseSunsetFooter()
{
  display.drawLine(0, 96, display.width(), 96, GxEPD_BLACK);
  display.drawLine(0, display.height() - 1,
                   display.width(), display.height() - 1, GxEPD_BLACK);

  if (!hasDay) {
    display.setFont(NULL);
    display.setTextSize(1);
    display.setCursor(4, 110);
    display.print("sun times pending...");
    return;
  }

  // DS-DIGI 14pt times, formatted "H:MMAM"/"H:MMPM" by the shared
  // formatTime12h(). Each half = 8-px sun glyph + 3-px gap + time, and is
  // centered in its quarter of the screen. The time width is measured per
  // half (single- vs double-digit hours differ) so both halves stay centered
  // — the longer AM/PM suffix is absorbed with no layout change.
  // Cursor y is the BASELINE.
  const int glyphY    = 104;   // 8-px glyph, vertically centered with the time
  const int baselineY = 116;   // 14pt: digits span y=99..116 (tallest ink ends at the baseline row; no time-string glyph descends below it)
  display.setFont(&DSDIGI14pt7b);

  const int leftCenter  = display.width() / 4;        // ~62
  const int rightCenter = display.width() * 3 / 4;    // ~187

  auto drawHalf = [&](const String &iso, int center,
                      const sprite::Sprite &glyph) {
    int hour = 0, minute = 0;
    bool valid = iso.length() >= 16;
    if (valid) {
      hour   = (iso[11] - '0') * 10 + (iso[12] - '0');
      minute = (iso[14] - '0') * 10 + (iso[15] - '0');
    }
    String sTime = formatTime12h(hour, minute);
    int timeW    = textWidth(sTime.c_str());
    const int glyphW = 8, gap = 3;
    int contentW = glyphW + gap + timeW;
    int leftX    = center - contentW / 2;

    sprite::draw(glyph, leftX, glyphY, 1);
    if (valid) {
      drawTime12hLeftAlign(DSDIGI14pt7b, hour, minute,
                           leftX + glyphW + gap, baselineY);
    }
  };

  drawHalf(daySunrise, leftCenter,  icons::SUN_RISING);
  drawHalf(daySunset,  rightCenter, icons::SUN_SETTING);
}

// ---- Misc helpers ----------------------------------------------------------

void WeatherDashboard::drawWaiting()
{
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  display.setCursor(3, 13);
  display.print(mode == MODE_NOW ? "Weather" : "Forecast");
  display.drawLine(0, 16, display.width(), 16, GxEPD_BLACK);

  display.setFont(NULL);
  display.setTextSize(2);
  display.setCursor(20, 60);
  display.print("WAITING");
  display.setTextSize(1);
  display.setCursor(20, 85);
  display.print("FOR DATA...");
}
