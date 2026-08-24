#pragma once
#include "dashboard.h"

// Static test pattern — the flashtool's verification screen. Renders with
// NO data and NO publisher (handlePayload is a no-op), so a freshly
// flashed device can be checked before any MQTT infrastructure exists.
// The pattern is deliberately asymmetric (labeled corners + orientation
// word) so a wrong-rotation flash is obvious at one glance.
class TestDashboard : public Dashboard {
public:
  const char* topic() const override { return "dashboard/test"; }
  const char* name()  const override { return "Test Pattern"; }
  void handlePayload(JsonDocument& doc) override;   // no-op
  void render() override;
};
