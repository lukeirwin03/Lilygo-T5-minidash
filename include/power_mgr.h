#pragma once
#include <cstdint>
#include <ctime>

namespace power_mgr {

enum WakeReason {
  WAKE_COLD_BOOT,   // first power-on or reset (no prior deep sleep)
  WAKE_TIMER,       // scheduled hourly wake
  WAKE_BUTTON,      // ext1 wake from BTN_NAV_PIN going low
};

WakeReason currentWakeReason();

// Fills *outTarget with the wall-clock instant of the next scheduled wake
// (the next WAKE_MINUTE:WAKE_SECOND, quiet-hours aware). Pure wall-clock
// computation — safe to call at render time, e.g. to show the user when the
// device will next wake; the actual sleep path re-computes and arms the
// timer independently. Returns false (with *outTarget zeroed) when the
// system clock isn't NTP-synced yet.
bool nextWakeTime(struct tm* outTarget);

// Sleeps until the next config::WAKE_MINUTE:WAKE_SECOND wall-clock instant,
// also arming ext1 on the nav button for early wake. Never returns —
// the chip cold-boots into setup() on wake.
void sleepUntilNextHourlyWake();

}
