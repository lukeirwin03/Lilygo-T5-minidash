#include "battery.h"
#include "config.h"
#include "util/pure.h"
#include <Arduino.h>
#include <esp_attr.h>

namespace battery {

// RTC slow memory: survives deep sleep, lost on full power-off / reset.
// -1 sentinel means "never sampled" — header renders "--%" until first
// sample() call lands.
static RTC_DATA_ATTR int s_lastMv = -1;

void sample() {
  // analogReadMilliVolts applies eFuse calibration when available, so we
  // get ~±2% absolute accuracy instead of the raw-ADC ±15% nonlinearity.
  // Default 11dB attenuation gives a ~0–2450 mV usable range at the pin,
  // which (×2 via the divider) covers 0–4.9 V — comfortably past LiPo full.
  analogSetPinAttenuation(config::BATTERY_ADC_PIN, ADC_11db);

  // The first read after wake tends to be noisy; discard it.
  (void)analogReadMilliVolts(config::BATTERY_ADC_PIN);

  uint32_t accum = 0;
  for (int i = 0; i < config::BATTERY_SAMPLES; i++) {
    accum += analogReadMilliVolts(config::BATTERY_ADC_PIN);
  }
  int pinMv = (int)(accum / config::BATTERY_SAMPLES);
  s_lastMv  = (int)(pinMv * config::BATTERY_DIVIDER);

  Serial.printf("[battery] %d mV at pin × %.2f = %d mV Vbat (~%d%%)\n",
                pinMv, config::BATTERY_DIVIDER, s_lastMv, lastPercent());
}

int lastMilliVolts() { return s_lastMv; }

// Piecewise-linear LiPo discharge curve. A single-slope fit (3300–4200 mV
// → 0–100%) wildly under-reports through the long ~3.7 V plateau, so
// these breakpoints follow the typical LiPo voltage→SoC curve at light
// load. Anything ≥ 4200 mV clamps to 100% (likely on USB / charging).
int lastPercent() {
  int mv = s_lastMv;
  if (mv < 0) return -1;            // never sampled — see header
  return pure::lipoPercent(mv);     // piecewise curve lives in util/pure.h
}

}
