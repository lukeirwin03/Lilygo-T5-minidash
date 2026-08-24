#include "power_mgr.h"
#include "config.h"
#include "util/pure.h"
#include "display_manager.h"

#include <Arduino.h>
#include <esp_sleep.h>
#include <time.h>

namespace power_mgr {

WakeReason currentWakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER: return WAKE_TIMER;
    case ESP_SLEEP_WAKEUP_EXT0:  return WAKE_BUTTON;
    case ESP_SLEEP_WAKEUP_EXT1:  return WAKE_BUTTON;
    default:                     return WAKE_COLD_BOOT;
  }
}

// True if `hour` falls inside the configured quiet range. Handles wrap
// across midnight; returns false when start == end (feature disabled).
static bool isQuietHour(int hour) {
  return pure::isQuietHour(hour, config::QUIET_START_HOUR, config::QUIET_END_HOUR);
}

// Returns microseconds to sleep until the next HH:WAKE_MINUTE:WAKE_SECOND
// wall-clock instant, skipping any wake whose hour falls in the configured
// quiet range. If the system clock isn't NTP-synced yet, falls back to a
// flat 1 hour so we don't accidentally schedule a sleep of decades.
// Fills `outTarget` with the normalized wall-clock target for logging and
// sets *outClockOk to false on the unsynced fallback, true on the normal
// path (so callers like nextWakeTime can tell the two apart). Serial
// logging is gated on `logToSerial`: render-time queries (nextWakeTime,
// which may run once per GxEPD2 refresh page) pass false so their output
// isn't duplicated ahead of the real sleep path's identical lines.
static uint64_t computeNextWakeUs(struct tm* outTarget, bool* outClockOk,
                                  bool logToSerial) {
  constexpr uint64_t ONE_HOUR_US = 3600ULL * 1000000ULL;
  constexpr time_t MIN_REASONABLE_EPOCH = 1700000000;  // 2023-11-14

  time_t now = time(nullptr);
  if (now < MIN_REASONABLE_EPOCH) {
    if (logToSerial)
      Serial.println("[power] Clock not synced — defaulting to 1 hour sleep");
    if (outTarget) memset(outTarget, 0, sizeof(*outTarget));
    if (outClockOk) *outClockOk = false;
    return ONE_HOUR_US;
  }

  struct tm lt;
  localtime_r(&now, &lt);

  struct tm target = lt;
  target.tm_min = config::WAKE_MINUTE;
  target.tm_sec = config::WAKE_SECOND;
  bool inFuture = (lt.tm_min  < config::WAKE_MINUTE) ||
                  (lt.tm_min == config::WAKE_MINUTE && lt.tm_sec < config::WAKE_SECOND);
  if (!inFuture) target.tm_hour += 1;

  // Normalize so we can inspect the actual wall-clock hour (mktime
  // returns the epoch, localtime_r re-fills tm with normalized fields).
  time_t targetEpoch = mktime(&target);
  localtime_r(&targetEpoch, &target);

  if (isQuietHour(target.tm_hour)) {
    if (logToSerial)
      Serial.printf("[power] Skipping wake at %02d:%02d:%02d — in quiet hours\n",
                    target.tm_hour, target.tm_min, target.tm_sec);
    target.tm_hour = config::QUIET_END_HOUR;
    target.tm_min  = config::WAKE_MINUTE;
    target.tm_sec  = config::WAKE_SECOND;
    targetEpoch = mktime(&target);
    // If the quiet range wraps midnight, QUIET_END_HOUR on the *same* day
    // can land in the past — bump one day forward and recompute.
    if (targetEpoch <= now) {
      target.tm_mday += 1;
      targetEpoch = mktime(&target);
    }
    localtime_r(&targetEpoch, &target);
  }

  long diffSec = (long)(targetEpoch - now);
  // Floor: never schedule less than 60 s (avoids busy-looping if the
  // clock just barely missed the target). Cap: 12 h is more than enough
  // for any sane quiet-hours configuration.
  if (diffSec < 60)         diffSec = 60;
  if (diffSec > 12 * 3600)  diffSec = 12 * 3600;
  if (outTarget)  *outTarget = target;
  if (outClockOk) *outClockOk = true;
  return (uint64_t)diffSec * 1000000ULL;
}

// Thin wrapper for the sleep path — discards the clock-ok flag, keeps
// the serial logging (the sleep path is where these lines belong).
static uint64_t sleepUntilNextWakeUs(struct tm* outTarget) {
  return computeNextWakeUs(outTarget, nullptr, true);
}

bool nextWakeTime(struct tm* outTarget) {
  if (!outTarget) return false;
  bool clockOk = false;
  computeNextWakeUs(outTarget, &clockOk, false);
  // Unsynced clock: *outTarget was zeroed by computeNextWakeUs.
  return clockOk;
}

void sleepUntilNextHourlyWake() {
  struct tm target;
  uint64_t us = sleepUntilNextWakeUs(&target);
  Serial.printf("[power] Deep sleep for %llu s (next wake at %02d:%02d:%02d)\n",
                us / 1000000ULL,
                target.tm_hour, target.tm_min, target.tm_sec);

  // Put the e-paper to its lowest-power state before we cut the rails.
  display.hibernate();

  esp_sleep_enable_timer_wakeup(us);
  // ext1 (ALL_LOW) fires when BTN_NAV_PIN is driven LOW (button pressed).
  // GPIO 39 is an RTC GPIO so it qualifies. ext1 is used instead of ext0
  // deliberately: ext0 forces the RTC peripherals domain to stay powered
  // for the whole deep sleep, raising the standby current the device sits
  // at ~24/7. ext1 latches the pad state without that domain — the button
  // has a board-side external pull-up, so no internal pull is needed and
  // the domain stays off. (Single selected pin + ALL_LOW == "this pin low".)
  esp_sleep_enable_ext1_wakeup(1ULL << config::BTN_NAV_PIN,
                               ESP_EXT1_WAKEUP_ALL_LOW);

  Serial.flush();
  esp_deep_sleep_start();
  // Unreachable — the chip cold-boots into setup() on wake.
}

}
