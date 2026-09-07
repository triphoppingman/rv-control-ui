#pragma once

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

#include "constants.h"

/**
 * @brief Network connection states shown on the NeoPixel status indicator.
 *
 * The precedence is Hotspot (red) above everything, then by station state:
 * no Wi-Fi is Starting (blue), Wi-Fi up but the broker down is BrokerDown
 * (orange), and both up is Connected (green).
 */
enum class StatusLedState : uint8_t { Starting, BrokerDown, Connected, Hotspot };

/**
 * @brief Drives the board's NeoPixel strip as a network status light.
 *
 * The whole strip is illuminated in the current state color. The indicator
 * runs dim so it reads in daylight without glaring in a dark RV. Problem and
 * transitional states (Starting, BrokerDown, Hotspot) stay lit, while the
 * Connected success state turns the strip off automatically after a short
 * delay so a healthy device goes dark. This class owns plain GPIO and never
 * touches LVGL.
 */
class StatusLed {
 public:
  /** @brief Return the one physical status indicator present on the board. */
  static StatusLed &instance();

  /** @brief Initialize the strip, clear all pixels, and show the starting state. */
  void begin();

  /** @brief Set the strip color for a new state; repeated same-state calls are no-ops. */
  void show(StatusLedState state);

  /** @brief Turn the strip off once the Connected auto-off delay has elapsed; call every pass. */
  void update();

 private:
  /** @brief Construct the singleton; use instance() to access the status indicator. */
  StatusLed();

  Adafruit_NeoPixel strip_;
  StatusLedState state_;
  bool begun_;
  // Set when the Connected state was shown; the strip turns off after the auto-off delay.
  uint32_t connectedAtMilliseconds_;
};
