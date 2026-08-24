#pragma once

namespace networking {
  // Bring up WiFi STA and block until associated + IP-configured,
  // bounded by config::WIFI_CONNECT_TIMEOUT_MS. Returns true on success,
  // false on timeout — the caller should fall back to replayCachedPayload()
  // instead of waiting for a fresh payload.
  bool connectWiFi();
  // Connect & subscribe to the dashboard topic(s), bounded by
  // config::MQTT_CONNECT_TIMEOUT_MS with escalating backoff. Returns true
  // on success, false on timeout (down broker, bad creds, ...) — the caller
  // should fall back to replayCachedPayload().
  bool connectMqtt();
  // Power the WiFi/MQTT radios down cleanly once the wake's data work is
  // done — no-ops when already off.
  void shutdown();
  void logHealth();
  bool isWiFiConnected();
  bool isMqttConnected();

  // Blocks up to timeoutMs waiting for SNTP to set the system clock.
  // Returns true once time(nullptr) reflects a real epoch (either it
  // was already synced from a prior boot's RTC value, or SNTP completed
  // within the window). Returns false on timeout — the caller should
  // assume wake-from-sleep alignment will fall back to a flat 1-hour
  // interval until NTP can complete on a later boot.
  bool waitForTimeSync(unsigned long timeoutMs);

  // Pumps mqtt.loop() until either the retained payload has been received
  // (dashboards[0]->hasData becomes true) or timeoutMs has elapsed.
  // Returns true if a payload was received during this call.
  bool pumpForPayload(unsigned long timeoutMs);

  // Replay the last MQTT payload from RTC RAM into the dashboards.
  // Returns true if a cached payload existed and was successfully parsed.
  bool replayCachedPayload();

  // True if any payload is currently cached in RTC RAM.
  bool hasCachedPayload();
}