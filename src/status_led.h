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
 * @brief Drives the board's NeoPixel strip as a single network status light.
 *
 * Only pixel 0 is used; the remaining pixels are cleared once at startup. The
 * indicator runs dim so it reads in daylight without glaring in a dark RV, and
 * it stays lit while the display backlight sleeps so the state remains visible
 * when the panel is dark. This class owns plain GPIO and never touches LVGL.
 */
class StatusLed {
 public:
  /** @brief Construct the strip wrapper for the configured status pin and pixel. */
  StatusLed();

  /** @brief Initialize the strip, clear all pixels, and show the starting state. */
  void begin();

  /** @brief Set the indicator color for a new state; repeated same-state calls are no-ops. */
  void show(StatusLedState state);

 private:
  Adafruit_NeoPixel strip_;
  StatusLedState state_;
  bool begun_;
};
