#include "status_led.h"

namespace {

using rv_control_ui::constants::kStatusLedBrightness;
using rv_control_ui::constants::kStatusLedCount;
using rv_control_ui::constants::kStatusLedPin;
using rv_control_ui::constants::kStatusLedStatusPixel;

/** @brief Pack a dim 8-bit RGB triplet into the NeoPixel 32-bit color form. */
uint32_t statusColor(uint8_t red, uint8_t green, uint8_t blue) {
  return (static_cast<uint32_t>(red) << 16U) | (static_cast<uint32_t>(green) << 8U) | static_cast<uint32_t>(blue);
}

}  // namespace

StatusLed::StatusLed()
    : strip_(kStatusLedCount, kStatusLedPin, NEO_GRB + NEO_KHZ800), state_(StatusLedState::Starting), begun_(false) {}

void StatusLed::begin() {
  strip_.begin();
  strip_.setBrightness(kStatusLedBrightness);
  // Clear every pixel first so the unused strip pixels stay dark.
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
      break;
    case StatusLedState::BrokerDown:
      color = statusColor(255, 96, 0);     // orange: Wi-Fi up, broker down
      break;
    case StatusLedState::Connected:
      color = statusColor(0, 255, 0);      // green: fully connected
      break;
    case StatusLedState::Hotspot:
      color = statusColor(255, 0, 0);      // red: setup hotspot active
      break;
  }
  strip_.setPixelColor(kStatusLedStatusPixel, color);
  strip_.show();
}
