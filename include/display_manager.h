#pragma once
#include <GxEPD2_BW.h>

extern GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display;

namespace display_mgr {
  void begin();
  void renderCurrentFull();
  void renderCurrentPartial();
  void renderLegend();             // abbreviation help screen (button-hold overlay)
  void switchTo(int newIndex);
  // Boot/status screen: weather wordmark + connectivity stats + a staged
  // progress bar. E-paper can't animate, so "progress" is real — callers
  // redraw at actual milestones (WiFi up, MQTT up) and the bar/marks
  // reflect what has actually happened.
  enum BootMark { BOOT_PENDING, BOOT_OK, BOOT_FAIL };
  void drawBootScreen(BootMark wifi, BootMark mqtt, const char* note);
  int currentIndex();
}