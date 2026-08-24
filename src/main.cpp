#include <Arduino.h>
#include <esp_chip_info.h>
#include "config.h"
#include "battery.h"
#include "button.h"
#include "dashboard.h"
#include "display_manager.h"
#include "networking.h"
#include "power_mgr.h"
#include "dashboards/weather_dashboard.h"

// -- Dashboards ---------------------------------------------------------------
// Two views of the same MQTT topic. Now is always the default on a scheduled
// wake; the user can short-press to flip to Forecast (button-wake path).
static WeatherDashboard weatherNowDash(WeatherDashboard::MODE_NOW);
static WeatherDashboard weatherForecastDash(WeatherDashboard::MODE_FORECAST);

// These are picked up via extern by display_manager and networking.
// `extern` here forces external linkage that `const` would otherwise hide.
extern Dashboard* const dashboards[] = { &weatherNowDash, &weatherForecastDash };
extern const size_t NUM_DASHBOARDS = sizeof(dashboards) / sizeof(dashboards[0]);

// -- Hardware -----------------------------------------------------------------
static Button btnNav(config::BTN_NAV_PIN);

// -- Diagnostics --------------------------------------------------------------
static void logBootInfo() {
  Serial.println();
  Serial.println("=========================================");
  Serial.println(" T5 Dashboard booting");
  Serial.println("=========================================");

  esp_chip_info_t chip;
  esp_chip_info(&chip);
  Serial.printf("  Chip:        ESP32 rev %d, %d cores\n",
                chip.revision, chip.cores);
  Serial.printf("  CPU freq:    %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("  Flash:       %u bytes\n", ESP.getFlashChipSize());
  Serial.printf("  Free heap:   %u bytes\n", ESP.getFreeHeap());
  Serial.printf("  SDK ver:     %s\n", ESP.getSdkVersion());
  Serial.printf("  Reset reason: %d\n", esp_reset_reason());
  Serial.println("-----------------------------------------");
}

// -- Wake handlers ------------------------------------------------------------
//
// Scheduled wake (cold boot or timer): fetch the freshest payload over MQTT,
// reset to the Now view, render, sleep.
//
static void handleScheduledWake(bool isColdBoot) {
  // Boot screen draws ONLY on cold boot — timer wakes never splash (the
  // bistable panel simply keeps the last dashboard image), and each stage
  // draw is a ~2-3 s refresh, acceptable only because cold boots are rare.
  // The stages redraw at real milestones so the marks/bar are honest.
  if (isColdBoot) {
    char note[48];
    snprintf(note, sizeof(note), "connecting to %s...", config::WIFI_SSID);
    display_mgr::drawBootScreen(display_mgr::BOOT_PENDING,
                                display_mgr::BOOT_PENDING,
                                note);
  }
  bool wifiOk = networking::connectWiFi();
  if (isColdBoot)
    display_mgr::drawBootScreen(wifiOk ? display_mgr::BOOT_OK : display_mgr::BOOT_FAIL,
                                display_mgr::BOOT_PENDING,
                                wifiOk ? "wifi up" : "wifi down");
  // Block briefly for SNTP. On cold boot the RTC clock is unset, and
  // power_mgr needs a real epoch to schedule the next wake on the hour
  // rather than falling back to a flat 1 hr (which would misalign every
  // subsequent wake). On a timer wake the RTC has persisted across
  // sleep, so this returns immediately. Harmless even when WiFi failed:
  // it just times out fast and returns.
  networking::waitForTimeSync(config::NTP_SYNC_TIMEOUT_MS);
  bool mqttOk = networking::connectMqtt();
  if (isColdBoot)
    display_mgr::drawBootScreen(wifiOk ? display_mgr::BOOT_OK : display_mgr::BOOT_FAIL,
                                mqttOk ? display_mgr::BOOT_OK : display_mgr::BOOT_FAIL,
                                mqttOk ? "fetching payload..." : "mqtt down");

  if (!wifiOk || !mqttOk) {
    // A bounded connect gave up — don't pump a dead or partial link
    // for a fresh payload; render whatever is cached in RTC RAM instead.
    Serial.println("[main] Network unavailable — rendering cached payload");
    networking::replayCachedPayload();
  } else {
    // Subscribe-triggered retained payload should arrive within ms. We give
    // it a hard deadline so a missing publisher never traps us awake.
    bool gotFresh = networking::pumpForPayload(config::PAYLOAD_WAIT_MS);
    if (!gotFresh) {
      Serial.println("[main] No fresh payload — falling back to cached");
      networking::replayCachedPayload();
    }
  }

  // Health log FIRST (while the radio state it reports is still live),
  // then kill the radio — the multi-second e-paper render below needs no
  // WiFi/MQTT, and the retained payload is already in RTC RAM.
  networking::logHealth();
  networking::shutdown();
  // Scheduled wakes always reset to the Now view (the 15:5 ratio idea is
  // expressed by Now being the default and the user pressing the button to
  // see Forecast on demand).
  display_mgr::switchTo(0);
}

//
// Button wake: no WiFi. Replay the cached payload, swap to the next view,
// then stay awake briefly so a sustained hold can still trigger the legend
// overlay.
//
static void handleButtonWake() {
  if (!networking::hasCachedPayload()) {
    Serial.println("[main] Button wake with no cached payload — boot screen + sleep");
    display_mgr::drawBootScreen(display_mgr::BOOT_PENDING,
                                display_mgr::BOOT_PENDING,
                                "no cached data yet");
    return;
  }

  networking::replayCachedPayload();
  display_mgr::switchTo(display_mgr::currentIndex() + 1);

  // Brief poll window for a long-press legend gesture. Released-early
  // legend dismissal lands here too.
  unsigned long start = millis();
  bool legendShown = false;
  while (millis() - start < config::BUTTON_WAKE_WINDOW_MS) {
    btnNav.poll();
    bool held = btnNav.isLongHeld();
    if (held && !legendShown) {
      legendShown = true;
      Serial.println("[main] Hold during wake window → legend");
      display_mgr::renderLegend();
    } else if (!held && legendShown) {
      Serial.println("[main] Hold released → return to dashboard");
      display_mgr::renderCurrentPartial();
      break;
    }
    delay(50);
  }
}

// -- Setup --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  // Run the whole awake window at 80 MHz (down from the 240 MHz default).
  // 80 MHz is the documented minimum for WiFi and is plenty for the
  // e-paper SPI traffic; the awake window — including the 5 s button-wake
  // poll loop — is a large share of this device's per-hour energy, so the
  // lower clock directly extends battery life. Serial 115200 and APB
  // peripherals are unaffected at this frequency.
  setCpuFrequencyMhz(80);
  logBootInfo();

  display_mgr::begin();
  btnNav.begin();

  // Sample the battery before WiFi comes up — radio TX droops the rails
  // ~50–100 mV, which would understate the indicator. Reading is cached
  // in RTC RAM, so the dashboards can render it on any wake path.
  battery::sample();

  power_mgr::WakeReason reason = power_mgr::currentWakeReason();
  switch (reason) {
    case power_mgr::WAKE_BUTTON:
      Serial.println("[main] Wake reason: BUTTON");
      handleButtonWake();
      break;
    case power_mgr::WAKE_TIMER:
      Serial.println("[main] Wake reason: TIMER");
      handleScheduledWake(false);
      break;
    case power_mgr::WAKE_COLD_BOOT:
    default:
      Serial.println("[main] Wake reason: COLD_BOOT");
      handleScheduledWake(true);
      break;
  }

  power_mgr::sleepUntilNextHourlyWake();
  // Unreachable — deep sleep cold-boots back into setup().
}

// loop() is never executed: setup() always ends in deep sleep.
void loop() {}
