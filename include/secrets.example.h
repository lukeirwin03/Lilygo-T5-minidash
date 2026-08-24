#pragma once

// Copy to `include/secrets.h` and fill in your network credentials.
// `include/secrets.h` should NOT be committed (see .gitignore).
// CI builds against these placeholder values directly (it copies this
// file over secrets.h), so they must stay compileable as-is; MQTT_USER /
// MQTT_PASS = nullptr means "no broker auth".

namespace secrets {
  constexpr const char* WIFI_SSID     = "your-ssid";
  constexpr const char* WIFI_PASSWORD = "your-password";

  constexpr const char* MQTT_USER = nullptr;   // or "user" if your broker requires auth
  constexpr const char* MQTT_PASS = nullptr;
}
