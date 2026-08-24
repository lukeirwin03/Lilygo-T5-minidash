#pragma once
#include <cstdint>
#include "secrets.h"

namespace config {
  // -- WiFi -- (credentials in secrets.h)
  constexpr const char* WIFI_SSID     = secrets::WIFI_SSID;
  constexpr const char* WIFI_PASSWORD = secrets::WIFI_PASSWORD;

  // -- MQTT --
  constexpr const char*    MQTT_HOST      = "192.168.1.38";
  constexpr uint16_t       MQTT_PORT      = 1883;
  constexpr const char*    MQTT_USER      = secrets::MQTT_USER;
  constexpr const char*    MQTT_PASS      = secrets::MQTT_PASS;
  constexpr const char*    MQTT_CLIENT_ID = "t5-dashboard";

  // -- Display behavior --
  constexpr unsigned long FULL_REFRESH_EVERY = 50;

  // -- Power / deep sleep --
  // Wake at the top of each hour (HH:00:00). The MQTT publisher emits
  // a retained message at :55 every hour, so on wake we subscribe and
  // receive the ~5-min-old retained payload. Local time is fine: every
  // hour wraps the same way under any timezone offset that's a
  // whole-hour shift.
  constexpr int  WAKE_MINUTE          = 0;
  constexpr int  WAKE_SECOND          = 0;
  // Quiet hours — any scheduled wake whose hour falls in [start, end) is
  // skipped, and the device sleeps straight through to the first wake at
  // or after QUIET_END_HOUR. Range may wrap midnight (e.g. start=22 /
  // end=6 means 10 PM through 6 AM). Set both to the same value to
  // disable.
  constexpr int  QUIET_START_HOUR     = 22;   // 10 PM
  constexpr int  QUIET_END_HOUR       = 6;    // 6 AM
  // After a button wake, stay awake briefly to handle hold-for-legend.
  constexpr unsigned long BUTTON_WAKE_WINDOW_MS = 5000;
  // Hard cap on how long the scheduled wake will wait for the retained
  // payload before sleeping with whatever it has (cached or empty).
  constexpr unsigned long PAYLOAD_WAIT_MS = 8000;
  // Hard cap on how long connectWiFi() will keep retrying before giving up
  // and letting the caller fall back to the cached payload. ~30 s reflects
  // the worst observed association time on a healthy AP.
  constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS  = 30000;
  // Same idea for connectMqtt(). Bounded so a down broker or bad creds
  // can never trap the device awake indefinitely.
  constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS  = 30000;
  constexpr unsigned long MQTT_RETRY_BACKOFF_BASE_MS = 2000;  // doubled per attempt, capped at 8 s
  // Max forecast entries buffered from the publisher's hours[] array.
  // Publisher currently sends 5 (offsets 1, 2, 3, 5, 8); extra slack for growth.
  constexpr int           FORECAST_HOURS_SHOWN = 8;

  // -- Hardware pins --
  constexpr int BTN_NAV_PIN    = 39;
  constexpr int EPD_CS_PIN     = 5;
  constexpr int EPD_DC_PIN     = 17;
  constexpr int EPD_RST_PIN    = 16;
  constexpr int EPD_BUSY_PIN   = 4;
  // Battery voltage sense on the LilyGo T5 V2.3.1: a 100k/100k divider
  // (ratio 0.5) routes Vbat into GPIO 35 = ADC1_CH7. ADC1 is WiFi-safe
  // (only ADC2 conflicts). If your board revision differs, change this.
  constexpr int BATTERY_ADC_PIN   = 35;
  constexpr int BATTERY_SAMPLES   = 16;     // averaged per sample() call
  constexpr float BATTERY_DIVIDER = 2.0f;   // mV_at_pin × this = Vbat_mV

  // -- Time --
  // POSIX TZ string. Examples:
  //   US Central (CST/CDT):  "CST6CDT,M3.2.0/2,M11.1.0/2"
  //   US Eastern  (EST/EDT): "EST5EDT,M3.2.0/2,M11.1.0/2"
  //   US Mountain (MST/MDT): "MST7MDT,M3.2.0/2,M11.1.0/2"
  //   US Pacific  (PST/PDT): "PST8PDT,M3.2.0/2,M11.1.0/2"
  //   No-DST Arizona:        "MST7"
  // The Mn.w.d/h triplet means "month n, week w, day-of-week d, hour h" —
  // DST start/end. The defaults above follow US rules (2nd Sun of March,
  // 1st Sun of November, 2 AM local).
  constexpr const char* TIMEZONE      = "CST6CDT,M3.2.0/2,M11.1.0/2";
  constexpr const char* NTP_SERVER_1  = "pool.ntp.org";
  constexpr const char* NTP_SERVER_2  = "time.nist.gov";
  // How long to block in waitForTimeSync() on cold boot. The RTC clock
  // persists across deep sleep once SNTP has succeeded at least once,
  // so subsequent wakes return from this near-instantly.
  constexpr unsigned long NTP_SYNC_TIMEOUT_MS = 10000;

}
