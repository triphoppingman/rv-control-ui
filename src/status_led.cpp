#include "status_led.h"

#include <Arduino.h>

namespace {

using rv_control_ui::constants::kStatusLedBrightness;
using rv_control_ui::constants::kStatusLedConnectedOffMilliseconds;
using rv_control_ui::constants::kStatusLedCount;
using rv_control_ui::constants::kStatusLedPin;

/** @brief Pack a dim 8-bit RGB triplet into the NeoPixel 32-bit color form. */
uint32_t statusColor(uint8_t red, uint8_t green, uint8_t blue) {
  return (static_cast<uint32_t>(red) << 16U) | (static_cast<uint32_t>(green) << 8U) | static_cast<uint32_t>(blue);
}

}  // namespace

StatusLed::StatusLed()
    : strip_(kStatusLedCount, kStatusLedPin, NEO_GRB + NEO_KHZ800),
      state_(StatusLedState::Starting),
      begun_(false),
      connectedAtMilliseconds_(0) {}

void StatusLed::begin() {
  strip_.begin();
  strip_.setBrightness(kStatusLedBrightness);
  strip_.clear();
  strip_.show();
  begun_ = true;
  // Force the first color write even though state_ already holds Starting.
  state_ = StatusLedState::Hotspot;
  show(StatusLedState::Starting);
}

void StatusLed::show(StatusLedState state) {
  if (!begun_) {
    state_ = state;
    return;
  }
  if (state == state_) return;
  state_ = state;
  uint32_t color = 0;
  switch (state) {
    case StatusLedState::Starting:
      color = statusColor(0, 0, 255);      // blue: booting / associating
      connectedAtMilliseconds_ = 0;
      break;
    case StatusLedState::BrokerDown:
      color = statusColor(255, 96, 0);     // orange: Wi-Fi up, broker down
      connectedAtMilliseconds_ = 0;
      break;
    case StatusLedState::Connected:
      color = statusColor(0, 255, 0);      // green: fully connected
      connectedAtMilliseconds_ = millis();
      break;
    case StatusLedState::Hotspot:
      color = statusColor(255, 0, 0);      // red: setup hotspot active
      connectedAtMilliseconds_ = 0;
      break;
  }
  strip_.fill(color);
  strip_.show();
}

void StatusLed::update() {
  // Only the Connected success state auto-turns off; problems and the hotspot
  // stay lit so they remain visible while the panel backlight is asleep.
  if (!begun_ || state_ != StatusLedState::Connected || connectedAtMilliseconds_ == 0) return;
  if (millis() - connectedAtMilliseconds_ < kStatusLedConnectedOffMilliseconds) return;
  connectedAtMilliseconds_ = 0;
  strip_.clear();
  strip_.show();
}
