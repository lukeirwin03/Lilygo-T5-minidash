# T5 Desk HUD

[![CI](https://github.com/YOUR_USERNAME/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/REPO/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
<!-- Swap YOUR_USERNAME/REPO in the CI badge link once the repo has a home. -->

Firmware for a LilyGo T5 2.13" e-paper board that renders a series of small MQTT-driven dashboards as a desk HUD. The weather dashboard is the first — a **Now** view with current conditions + today's outlook, and a **Forecast** view showing four upcoming hours — fed by a single retained MQTT topic; the firmware cycles between the flashed views automatically and on button press. See [Adding a dashboard](#adding-a-dashboard) for the extension path.

![layout](https://placeholder.invalid/dashboard.png)
<!-- Replace the placeholder above with a real screenshot once one exists. -->

## Hardware

| Part            | Detail                                              |
| --------------- | --------------------------------------------------- |
| Board           | LilyGo T5 V2.3.1 (ESP32 + 2.13" e-paper)            |
| Panel           | DEPG0213BN — driven by `GxEPD2_213_BN`              |
| Resolution      | 250 × 122, landscape (`setRotation(1)`)             |
| Display pins    | CS=5, DC=17, RST=16, BUSY=4                          |
| User button     | GPIO 39 (active-low, board-side pull-up)            |

All pin assignments live in [`include/config.h`](include/config.h).

## What it does

The firmware spends almost all of its time in **deep sleep**. Each cycle:

1. Wake up — either from the RTC timer (scheduled at `HH:WAKE_MINUTE:WAKE_SECOND`, default `:00:00` — the top of every hour) or from a button press (ext1 wake on GPIO 39).
2. Branch on the wake reason:
   - **Scheduled wake / cold boot** → connect WiFi, block up to `NTP_SYNC_TIMEOUT_MS` (default 10 s) for SNTP so the next wake can be aligned to a real wall clock, connect MQTT, wait up to `PAYLOAD_WAIT_MS` (default 8 s) for the retained payload, fall back to the RTC-cached payload if nothing fresh arrived, reset to the **Now** view, render.
   - **Button wake** → no radio. Replay the cached payload from RTC RAM, swap to the next view, poll the button for `BUTTON_WAKE_WINDOW_MS` (default 5 s) so a sustained hold can still trigger the legend overlay.
3. Hibernate the e-paper, arm the next wake (timer + ext1), and `esp_deep_sleep_start()`. The timer wake skips any hour inside the configured quiet range — by default the device sleeps straight through `22:00`–`06:00`, then resumes at the next regular wake (`06:00:00`).

The publisher emits its retained MQTT message at `:55`, so a wake at `HH:00:00` subscribes and receives the ~5-min-old retained payload immediately. The header time on the dashboard is the payload's own `hours[0].time`, not wall-clock, so a missed publish surfaces visibly there.

The panel keeps its last rendered image with zero current draw, so the screen stays "on" between wakes. Two `WeatherDashboard` instances (`MODE_NOW`, `MODE_FORECAST`) both parse the shared retained payload — switching between them is just a re-render from cached state.

- **Short button press** cycles through the flashed dashboard set (all: Now → Forecast → Test Pattern; weather-only: Now ↔ Forecast; test-only: the single static screen) and sleeps showing it.
- **Long press (≥1 s)** during the 5 s post-wake window overlays the abbreviation legend; release returns to the dashboard.
- **`FULL_REFRESH_EVERY` partials** (default 50) → one forced full refresh to clear ghosting. Counter lives in RTC RAM so it survives sleep.

## MQTT model

The firmware is a pure subscriber — it never publishes. A single topic feeds both views.

```
publisher (cron, script, integration)
        │   sets retain=true
        ▼
   Mosquitto / any MQTT broker
        │   stores latest as retained
        ▼
   T5 ESP32 subscribes on connect → both
   WeatherDashboards parse → renderer
   draws whichever view is current
```

**Topic:** `dashboard/forecast`

**Buffer:** the MQTT client buffer is bumped to 4 KB (`mqtt.setBufferSize(4096)`) — the default 256 B truncates the weather payload mid-message.

**Retain matters.** Publishers must set the retain flag (`mosquitto_pub -r ...`) so the broker hands the latest message to the ESP32 immediately on connect. Without retain the dashboards stay blank until the next publish.

### Payload schema

The publisher should emit JSON shaped like [`ex-payload.json`](ex-payload.json):

```jsonc
{
  "location": "Omaha, Nebraska",
  "updated":  "2026-05-10T19:37:04Z",      // ISO; parsed but currently unused in rendering
  "model":    "open-meteo-best-match",

  "days": [
    {
      "sunrise":              "2026-05-10T06:10",  // ISO; HH:MM is parsed
      "sunset":               "2026-05-10T20:30",
      "weather_code":          3,                  // WMO code
      "high_f":                73.5,
      "low_f":                 42.3,
      "feels_like_high_f":     67.8,
      "feels_like_low_f":      36.9,
      "precip_in":             0.0,
      "precip_prob_max":       2,
      "snow_in":               0.0,
      "uv_index_max":          7.55,
      "wind_max_mph":          11.2,
      "wind_gust_max_mph":     22.8,
      "wind_dir_dominant_deg": 315,
      "daylight_sec":          51596,
      "sunshine_sec":          48368
    }
    // additional days are ignored; only days[0] is used
  ],

  "hours": [
    {
      "offset_h":         1,                       // hours ahead of "now"
      "time":             "2026-05-10T15:00",      // ISO local; HH:MM of hours[0] drives the header DS-DIGI time
      "temp_f":           72.4,
      "feels_like_f":     67.5,
      "dew_point_f":      32.5,
      "humidity_pct":     23,
      "precip_prob":      1,
      "precip_in":        0.0,
      "snow_in":          0.0,
      "weather_code":     0,
      "pressure_hpa":     1021.1,
      "cloud_cover_pct":  2,
      "visibility_m":     242125.0,
      "wind_mph":         11.2,
      "wind_dir_deg":     290,
      "wind_gust_mph":    22.8,
      "uv_index":         7.1,
      "is_day":           1
    }
    // typical publisher sends 5 entries at offsets 1, 2, 3, 5, 8
  ]
}
```

`hours[0]` is treated as the headline ("current conditions") in the Now view. The Forecast view renders four columns at offsets 1, 3, 5, and 8 (matched by each entry's `offset_h`; a missing offset renders `--`).

## The two dashboards

### Now view (`MODE_NOW`)

```
┌──────────────────────────────────────────────────────┐
│ Omaha, Nebraska                vld til 3:00PM        │ ← header: location + "data is for" time
├──────────────────────────────────────────────────────┤
│ [icon]    [   big DS-DIGI temp °F  ] │ UV 7 hi      │
│ 32×32     [    ▲ 73    ▼ 42       ] │ feels 67     │ ← profile-driven stats
│                                     │ humid 23%    │
├─────────────────────────────────────┴────────────────┤
│         ☼↑  06:10AM         ☼↓  08:30PM              │ ← sunrise / sunset
└──────────────────────────────────────────────────────┘
```

The **header time** is the headline forecast hour (`hours[0].time`) plus one hour, not wall-clock — since the device deep-sleeps between updates, a wall clock would lie by up to 60 min. The small `vld til` label sells that this is "data is valid until then," not "now."

The **right column** picks three stats per profile that are actually relevant to the current weather — no "always feels" line:

| Profile          | Trigger                                       | Three lines (top → bottom)         |
| ---------------- | --------------------------------------------- | ---------------------------------- |
| `WINDY`          | wind ≥ 20 mph or gust ≥ 30 mph                | compass+wind, gust, feels          |
| `STORM`          | WMO 95/96/99                                  | rain inches, gust, rain %          |
| `SNOW`           | WMO 71–77/85/86 or `snow_in > 0`              | snow inches, cloud %, feels        |
| `RAIN`           | WMO 51–67/80–82 or `precip_in > 0`            | rain inches, cloud %, rain %       |
| `FOG`            | WMO 45/48                                     | visibility mi, humid, feels        |
| `CLEAR_DAY`      | WMO 0 + day                                   | UV, feels, humid                   |
| `CLEAR_NIGHT`    | WMO 0 + night                                 | feels, humid, dew point            |
| `PARTLY_CLOUDY`  | WMO 1/2 — day                                 | UV, cloud %, feels                 |
| `PARTLY_CLOUDY`  | WMO 1/2 — night                               | feels, cloud %, humid              |
| `OVERCAST`       | WMO 3                                         | cloud %, feels, rain %             |

The **bottom band** shows today's sunrise and sunset — `☼↑ HH:MMAM` on the left, `☼↓ HH:MMPM` on the right, with DS-DIGI times for legibility.

### Forecast view (`MODE_FORECAST`)

Four hourly columns (`+1H` / `+3H` / `+5H` / `+8H`) over a tightened daily-stats panel and a sleep footer strip:

```
┌──────────────────────────────────────────────────────┐
│ Omaha, Nebraska                vld til 3:00PM        │
├─────────────┬─────────────┬────────────┬─────────────┤
│     +1H     │     +3H     │    +5H     │     +8H     │
│  [icon32]   │  [icon32]   │  [icon32]  │  [icon32]   │
│      73     │      73     │     72     │      68     │
├─────────────┴─────────────┴────────────┴─────────────┤
│ hi 73  lo 42            wind WNW 15                  │
│ UV max 7                rain 5%                      │
├──────────────────────────────────────────────────────┤
│ zzz sleeping            next update 1:00AM           │
└──────────────────────────────────────────────────────┘
```

Hourly columns are matched by `offset_h` (1, 3, 5, 8) — if the publisher's schedule changes, missing offsets render `--` rather than mis-labeling a column. Each column stacks a 32×32 icon over bold DS-DIGI digits (14pt), centered in its ~62-px column, so the temperature is glanceable across the room. Daily stats sit at a tight 12-px line spacing in `FreeSansBold9pt7b` (none of the daily strings have descenders): hi/lo + max wind (with compass) on top, UV max + max rain probability below. Sunrise/sunset lives in the Now-view footer where its DS-DIGI times have more room to breathe.

The **sleep footer strip** is the resting state of the sleep screen: the bistable panel keeps the rendered image with zero current draw for the whole deep-sleep interval, so "zzz sleeping" plus the quiet-hours-aware `next update H:MMAM` (from `power_mgr::nextWakeTime()`) tells the reader exactly when fresh data will appear — overnight it shows the next real wake, e.g. `6:00AM`, since wakes inside the quiet range are skipped. Like the Now view, the header time is `hours[0].time` + 1h driving `vld til` on both views.

### Legend overlay

Hold the button for ≥1 s to overlay an abbreviation legend (rendered by `display_mgr::renderLegend()` → `drawLegend()` in [`src/display_manager.cpp`](src/display_manager.cpp)). Release returns to the current dashboard via partial refresh.

## Configuration

All tunables live in [`include/config.h`](include/config.h). The most likely ones to change:

| Setting                        | Default       | Purpose                                                  |
| ------------------------------ | ------------- | -------------------------------------------------------- |
| `MQTT_HOST`, `MQTT_PORT`       | `192.168.1.38`, `1883` | Broker address                                  |
| `MQTT_CLIENT_ID`               | `t5-dashboard`| Base name reported to the broker; a per-device MAC suffix is appended at connect time so dedicated desk devices don't evict each other's sessions |
| `WAKE_MINUTE`, `WAKE_SECOND`   | `0`, `0`      | Wall-clock instant of each hourly RTC wake (top of the hour by default; publisher emits at `:55` so we pull the retained payload) |
| `QUIET_START_HOUR`, `QUIET_END_HOUR` | `22`, `6`    | Skip wakes during `[start, end)`; defaults sleep through 10 PM–6 AM. Set both equal to disable. Wraps midnight if start > end. |
| `PAYLOAD_WAIT_MS`              | `8000`        | Max time the scheduled wake waits for a fresh MQTT payload |
| `NTP_SYNC_TIMEOUT_MS`          | `10000`       | Max time `waitForTimeSync()` blocks on cold boot. Subsequent wakes return immediately (RTC persists). |
| `BUTTON_WAKE_WINDOW_MS`        | `5000`        | Post-button-wake window for hold-for-legend detection    |
| `FULL_REFRESH_EVERY`           | `50`          | Partial refreshes before a forced full refresh           |
| `FORECAST_HOURS_SHOWN`         | `8`           | Capacity of the forecast buffer (publisher sends 5)      |
| `BTN_NAV_PIN`, `EPD_*_PIN`     | board pins    | Hardware wiring                                          |

WiFi credentials and MQTT auth live in [`include/secrets.h`](include/secrets.h), which is gitignored. Copy [`include/secrets.example.h`](include/secrets.example.h) → `include/secrets.h` and fill in your values before flashing. Values are still baked into the firmware image at compile time — handle accordingly.

## File layout

```
project-root/
├── .editorconfig                ← editor-neutral formatting rules
├── .github/workflows/ci.yml     ← CI: stub-secrets build + native tests
├── LICENSE                      ← MIT
├── platformio.ini
├── convert_fonts.sh             ← TTF → Adafruit GFX header converter (regenerates include/fonts/)
├── flash.sh                     ← per-hand × per-dash flash wrapper (esp32dev[_<dash>][_left])
├── ex-payload.json              ← reference MQTT payload
├── README.md
├── fonts_ttf/                   ← source TrueType fonts (input to convert_fonts.sh)
├── include/
│   ├── config.h                 ← tunables: broker, timing, pins (pulls secrets.h)
│   ├── secrets.h                ← WiFi + MQTT credentials (gitignored)
│   ├── secrets.example.h        ← template for the above
│   ├── button.h
│   ├── dashboard.h              ← abstract base class
│   ├── display_manager.h
│   ├── networking.h
│   ├── power_mgr.h              ← deep-sleep + wake-reason helpers
│   ├── dashboards/
│   │   ├── weather_dashboard.h
│   │   └── test_dashboard.h      ← static test pattern (flashtool verification)
│   ├── fonts/                   ← DS-DIGI TrueType → Adafruit GFX (generated; ~240 KB PROGMEM)
│   │   ├── DSDIGI10pt7b.h       ← (generated; unused)
│   │   ├── DSDIGI12pt7b.h       ← header time digits (AM/PM suffix in built-in font)
│   │   ├── DSDIGI14pt7b.h       ← forecast column temps + sunrise/sunset times + boot-screen subtitle
│   │   ├── DSDIGI18pt7b.h       ← Now-view hi/lo digits
│   │   ├── DSDIGI24pt7b.h       ← big-temp "F"
│   │   ├── DSDIGI32pt7b.h       ← big now-temperature + boot 'WEATHER' wordmark
│   │   └── DSDIGI48pt7b.h       ← (generated; unused)
│   └── graphics/
│       ├── icons.h              ← 32×32 weather + 8×8 sunrise/sunset sprites (declarations)
│       └── sprite.h
└── src/
    ├── main.cpp                 ← entry point, dashboard registry, main loop
    ├── button.cpp
    ├── display_manager.cpp      ← rendering, legend overlay, boot screen
    ├── networking.cpp           ← WiFi + MQTT lifecycle + dispatch + RTC payload cache
    ├── power_mgr.cpp            ← sleep alignment + ext1/timer wake config
    ├── dashboards/
    │   ├── weather_dashboard.cpp
    │   └── test_dashboard.cpp
    └── graphics/
        ├── icons.cpp
        └── sprite.cpp
```

### Module rundown

- **`main.cpp`** — single-shot `setup()`: reads the wake reason from `power_mgr`, branches to either `handleScheduledWake()` (cold boot / timer wake → WiFi+MQTT) or `handleButtonWake()` (replay cached payload, swap view, brief button-poll window for legend), then sleeps. `loop()` is intentionally empty.
- **`button.cpp`** — 30 ms-debounced single-button driver. Distinguishes short vs long press (`LONG_PRESS_MS = 1000`). Initializes `pressStartMs_` at boot if the pin is already LOW, so an ext1 wake-while-held interprets the gesture correctly.
- **`display_manager.cpp`** — owns the singleton `GxEPD2_BW` display. Stores `currentDashboard` and `partialCount` in RTC slow memory so they survive deep sleep. Centralizes full/partial render loops, dashboard switch, the staged boot/status screen (WiFi/MQTT marks + progress bar), and legend overlay.
- **`networking.cpp`** — blocking WiFi + MQTT connect for the scheduled-wake path, `waitForTimeSync()` so the next wake can be aligned to real wall-clock time, `pumpForPayload()` to wait for the retained message with a timeout, RTC-RAM payload cache (`RTC_PAYLOAD_CAP = 3 KB`), and `replayCachedPayload()` so a button wake can render without bringing the radio up. Caches the AP's channel/BSSID in RTC RAM for scan-free fast reconnect on subsequent wakes; `shutdown()` powers the radio off before the multi-second render once the wake's data work is done. Every received MQTT message is dispatched to **every** dashboard whose `topic()` matches.
- **`power_mgr.cpp`** — computes microseconds until the next `HH:WAKE_MINUTE:WAKE_SECOND` from NTP-synced local time, arms `esp_sleep_enable_timer_wakeup()` plus `esp_sleep_enable_ext1_wakeup()` (ALL_LOW) on `BTN_NAV_PIN` — ext1 over ext0 keeps the RTC peripheral domain powered down during deep sleep — hibernates the e-paper, calls `esp_deep_sleep_start()`. The same wall-clock computation is exposed render-time-safe as `nextWakeTime()`, which the forecast view's sleep footer uses to print the next-update time before sleeping. If the clock isn't yet synced (e.g. NTP failed during the boot window) it falls back to a flat 1-hour sleep and gets another chance to align on the next boot.
- **`dashboards/weather_dashboard.cpp`** — payload parsing into `current*` / `day*` / `forecast[]` state; both `renderNow()` and `renderForecast()`. Contains the profile detection and the layout code for the inverted header, big DS-DIGI temperature, hi/lo row with triangle glyphs, vertical/horizontal separators, profile-driven right column, and the sunrise/sunset bottom band.
- **`dashboards/test_dashboard.cpp`** — static test pattern (labeled corners + orientation word) used by `flash.sh` to verify a freshly flashed device with no MQTT infrastructure; renders from `DISPLAY_ROTATION` alone, ignores payloads.
- **`include/fonts/DSDIGI{10,12,14,18,24,32,48}pt7b.h`** — the DS-DIGI digital-watch TrueType face, converted to Adafruit GFX PROGMEM headers by `convert_fonts.sh`. All digits/times render through these via `display.setFont()` + `display.print()`. The degree sign is **not** in DS-DIGI, so the big-temperature ° is still drawn as a small circle.
- **`graphics/icons.cpp`** — 1-bit packed PROGMEM sprites: a 32×32 weather icon set (sun, partly cloudy, cloudy, rain, heavy rain, snow, thunderstorm, fog) plus 8×8 utility glyphs (sun-rising / sun-setting). `forWeatherCodeBig()` maps WMO codes to a 32×32 icon for both Now (current conditions) and Forecast (per-hour) views.
- **`graphics/sprite.cpp`** — generic 1-bpp sprite blitter with integer scaling.

### How a message becomes pixels

1. Publisher sends retained JSON to `dashboard/forecast` (typically at `:55` past every hour).
2. ESP32 wakes from deep sleep at `HH:00:00`, connects WiFi, blocks briefly in `waitForTimeSync()` (instant on subsequent wakes since the RTC persists), then connects MQTT and subscribes.
3. Broker immediately forwards the retained message; `networking::onMessage` copies it to RTC RAM and parses it.
4. **Both** `WeatherDashboard` instances whose `topic()` matches are dispatched, each parses independently.
5. `display_mgr::switchTo(0)` resets to the Now view and calls `renderCurrentPartial()`.
6. `WeatherDashboard::render()` dispatches to `renderNow()` or `renderForecast()` based on the instance's mode.
7. GxEPD2 ships the framebuffer to the panel.
8. `power_mgr::sleepUntilNextHourlyWake()` hibernates the panel and deep-sleeps until the next `HH:00:00`.

## Adding a dashboard

1. Create `include/dashboards/foo.h` and `src/dashboards/foo.cpp` subclassing `Dashboard`. Implement `topic()`, `name()`, `handlePayload(JsonDocument&)`, and `render()`.
2. In `main.cpp`, add the include, instantiate `FooDashboard fooDash;`, and append `&fooDash` to the `dashboards[]` array (the `NUM_DASHBOARDS` count is computed automatically).
3. Have a publisher emit retained JSON on whatever topic `topic()` returns.
4. Make it flash-selectable as a single-device build:
   - guard the instance + registry entry with an `ENABLE_FOO` block in `main.cpp` (pattern: the `Dashboard set` block),
   - add `esp32dev_foo` / `esp32dev_foo_left` envs to `platformio.ini` (extend the base envs, zeroing the other `ENABLE_` flags),
   - extend `flash.sh`'s `--dash` validation + env mapping, and
   - add the new envs to CI's build list.

No changes to networking, dispatch, button handling, or rendering are needed — the main loop just iterates one more entry and `renderCurrentPartial()` calls your `render()`.

## Build & flash

```bash
# from project root
pio run                    # compile
pio run -t upload          # compile + flash via configured upload_port
pio device monitor         # 115200 baud, exception decoder filter
```

Upload port and monitor port are auto-detected by PlatformIO (see [`platformio.ini`](platformio.ini)); override with `--upload-port` / `--monitor-port`, or pin a port in `platformio.ini`, if your dev machine needs it. If sprites or fonts look stale after a layout change, `pio run -t clean` first to drop the cached build tree.

### Flashing & device orientation

[`flash.sh`](flash.sh) wraps the upload for the two device axes:

- **`--hand left|right`** (default right) — mounting: right flashes the default landscape builds (`setRotation(1)`), left flashes the 180°-rotated `_left` builds (`setRotation(3)`) for devices sitting flipped on the desk's left side.
- **`--dash all|weather|test`** (default all) — which dashboards get baked in: the full cycle, weather Now+Forecast only, or the static test pattern only. Each desk device carries just its designated set.

The env naming scheme is `esp32dev[_<dash>][_left]` (e.g. `esp32dev_weather_left`); `--port` disambiguates multiple attached boards, `--monitor` opens the serial monitor after flashing, and CI builds every dashboard-set × hand combination on every push/PR. Example for a two-device desk: a left-mounted weather unit with `./flash.sh --dash weather --hand left`, and a right-mounted test unit with `./flash.sh --dash test --hand right`. Run `./flash.sh --help` for the full option list.

## Testing

```bash
pio test -e native    # pure-function unit tests, no hardware needed
```

CI builds every dashboard-set × hand combination (all six `esp32dev*` envs) and runs these tests on every push and pull request — see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## Diagnostics

Serial monitor at 115200 baud prints:

- Boot info — chip revision, CPU, flash size, free heap, reset reason
- WiFi — SSID, IP, gateway, DNS, subnet, RSSI, MAC, channel on connect
- MQTT — connect attempts with decoded state names, subscribe ACKs per topic
- Every received message — topic, byte count, dispatched-to dashboard names
- Each parsed payload — temp, code, hour count, day-present flag
- Display events — full vs partial refreshes, dashboard switches, legend show/hide
- Health snapshot — uptime, free heap, WiFi/MQTT state, logged once per scheduled wake (button wakes skip the radio, so no snapshot is emitted)

Most issues (wrong credentials, broker unreachable, malformed payload, payload too large for the buffer) surface clearly in this output.

## License

MIT — see [LICENSE](LICENSE).

### Third-party assets

- The DS-DIGI font (`fonts_tff/DS-DIGI.TTF`, author "Dunhill") is freeware — verify its terms fit your use before redistributing. The generated `include/fonts/DSDIGI*.h` headers are derived from it.
- Runtime libraries pulled in via `lib_deps` carry their own licenses: GxEPD2, Adafruit GFX, PubSubClient, ArduinoJson.
