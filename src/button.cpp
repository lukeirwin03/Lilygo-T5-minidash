#include "button.h"

Button::Button(int pin) : pin_(pin) {}

void Button::begin() {
  // NB: the default BTN_NAV_PIN (GPIO 39) is input-only on the ESP32 and has
  // no internal pull-up/down. We rely on the board-side external pull-up;
  // INPUT_PULLUP here would do nothing. If you re-pin to GPIO 0-32, switch
  // to INPUT_PULLUP and drop the external pull-up.
  pinMode(pin_, INPUT);
  rawState_ = digitalRead(pin_);
  debouncedState_ = rawState_;
  lastChangeMs_ = millis();
  // If we boot/wake with the button already held (ext1 wake on LOW),
  // treat boot time as the press start so isLongHeld()/short vs long
  // detection compute the correct held duration.
  if (rawState_ == LOW) pressStartMs_ = millis();
}

void Button::poll() {
  int reading = digitalRead(pin_);
  if (reading != rawState_) {
    lastChangeMs_ = millis();
    rawState_ = reading;
  }
  if (millis() - lastChangeMs_ > DEBOUNCE_MS && reading != debouncedState_) {
    debouncedState_ = reading;
    if (debouncedState_ == LOW) {
      pressStartMs_ = millis();
    } else {
      unsigned long held = millis() - pressStartMs_;
      if (held >= LONG_PRESS_MS) longPressPending_ = true;
      else                       shortPressPending_ = true;
    }
  }
}

bool Button::wasShortPress() {
  if (shortPressPending_) { shortPressPending_ = false; return true; }
  return false;
}

bool Button::wasLongPress() {
  if (longPressPending_) { longPressPending_ = false; return true; }
  return false;
}

bool Button::isLongHeld() const {
  return debouncedState_ == LOW &&
         (millis() - pressStartMs_) >= LONG_PRESS_MS;
}