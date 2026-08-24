#pragma once
#include <Arduino.h>

class Button {
public:
  explicit Button(int pin);
  void begin();
  void poll();
  bool wasShortPress();
  bool wasLongPress();
  // True if the button is currently held AND has been held past LONG_PRESS_MS.
  // Stays true while held; goes false on release.
  bool isLongHeld() const;

private:
  static constexpr unsigned long DEBOUNCE_MS   = 30;
  static constexpr unsigned long LONG_PRESS_MS = 1000;
  int pin_;
  int rawState_       = HIGH;
  int debouncedState_ = HIGH;
  unsigned long lastChangeMs_ = 0;
  unsigned long pressStartMs_ = 0;
  bool shortPressPending_ = false;
  bool longPressPending_  = false;
};