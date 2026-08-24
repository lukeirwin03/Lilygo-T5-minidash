#pragma once
#include <cstddef>

// Hardware-free, dependency-free pure helpers extracted from the firmware
// so they can be unit-tested under a native (non-ESP32) PlatformIO env.
//
// Logic is copied VERBATIM from the original static helpers in
// src/dashboards/weather_dashboard.cpp, src/battery.cpp, and
// src/power_mgr.cpp — same bin edges, breakpoints, and rounding. The point
// is testability, not retuning; any behavior change here is a bug.

namespace pure {

// 8-point compass label from wind direction in degrees. "N" at 0/360,
// bin boundaries at +/-22.5 deg. Handles negatives and values > 360.
inline const char* compass8(int deg)
{
  static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  int d = ((deg % 360) + 360) % 360;
  return names[((d + 22) / 45) % 8];
}

// UV intensity category: <3 lo, <6 md, <8 hi, <11 vh, else ex.
inline const char* uvCode(float uv)
{
  if (uv < 3.0f)  return "lo";
  if (uv < 6.0f)  return "md";
  if (uv < 8.0f)  return "hi";
  if (uv < 11.0f) return "vh";
  return "ex";
}

// Convert 24h hour (0..23) to 12h. Returns 1..12; sets pmOut.
// hour24==0 -> 12 (am); hour24==12 -> 12 (pm); hour24==13 -> 1 (pm).
inline int to12h(int hour24, bool& pmOut)
{
  int h12 = hour24 % 12;
  if (h12 == 0) h12 = 12;
  pmOut = hour24 >= 12;
  return h12;
}

// Parse "YYYY-MM-DDTHH:MM" (needs len >= 16). On success sets hour (0..23)
// and minute (0..59) and returns true. Returns false if len < 16.
inline bool parseIsoHourMinute(const char* iso, size_t len, int& hour, int& minute)
{
  if (len < 16) return false;
  hour   = (iso[11] - '0') * 10 + (iso[12] - '0');
  minute = (iso[14] - '0') * 10 + (iso[15] - '0');
  return true;
}

// True if `hour` falls in [start, end) with midnight wrap when start > end.
// Returns false when start == end (feature disabled).
inline bool isQuietHour(int hour, int start, int end)
{
  if (start == end) return false;
  if (start < end)  return hour >= start && hour < end;
  return hour >= start || hour < end;        // wraps midnight
}

// LiPo state-of-charge percent from battery millivolts via a piecewise
// discharge curve. 3300 -> 0, 4200 -> 100, clamped. Matches battery.cpp's
// current breakpoints exactly. Does NOT handle the -1 "never sampled"
// sentinel — that stays in battery.cpp.
inline int lipoPercent(int mv)
{
  if (mv >= 4200) return 100;
  if (mv >= 4100) return 95 + (mv - 4100) *  5 / 100;   // 4100->95, 4200->100
  if (mv >= 4000) return 85 + (mv - 4000) * 10 / 100;   // 4000->85
  if (mv >= 3900) return 75 + (mv - 3900) * 10 / 100;   // 3900->75
  if (mv >= 3800) return 55 + (mv - 3800) * 20 / 100;   // 3800->55
  if (mv >= 3700) return 35 + (mv - 3700) * 20 / 100;   // 3700->35
  if (mv >= 3600) return 20 + (mv - 3600) * 15 / 100;   // 3600->20
  if (mv >= 3400) return  5 + (mv - 3400) * 15 / 200;   // 3400->5
  if (mv >= 3300) return      (mv - 3300) *  5 / 100;   // 3300->0
  return 0;
}

}  // namespace pure
