#pragma once

#include <stddef.h>
#include <stdint.h>

#include "display_catalog.h"

/**
 * @brief Compile-time board and UI geometry constants for RV Control UI.
 *
 * These values describe the Elecrow CrowPanel Advance 1.28-inch rotary
 * display and the fixed SquareLine layout used by the firmware. Keep hardware
 * pin assignments aligned with the known-good Elecrow RotaryScreen reference.
 */
namespace rv_control_ui::constants {

/** @brief Native GC9A01 panel width in pixels. */
constexpr uint32_t kScreenWidth = 240;
/** @brief Native GC9A01 panel height in pixels. */
constexpr uint32_t kScreenHeight = 240;

/** @brief CST816D touch-controller I2C data pin. */
constexpr int kTouchSdaPin = 6;
/** @brief CST816D touch-controller I2C clock pin. */
constexpr int kTouchSclPin = 7;
/** @brief CST816D touch-controller interrupt pin. */
constexpr int kTouchInterruptPin = 5;
/** @brief CST816D touch-controller reset pin. */
constexpr int kTouchResetPin = 13;

/** @brief Board power-light control pin. */
constexpr int kPowerLightPin = 40;
/** @brief Rotary encoder quadrature A pin. */
constexpr int kEncoderAPin = 45;
/** @brief Rotary encoder quadrature B pin. */
constexpr int kEncoderBPin = 42;
/** @brief Rotary encoder push-button pin. */
constexpr int kEncoderButtonPin = 41;
/** @brief PWM-controlled display backlight pin. */
constexpr int kBacklightPin = 46;

/** @brief Initial user-selected display brightness percentage. */
constexpr uint8_t kDefaultBrightnessPercent = 50;
/** @brief Minimum elapsed time between accepted encoder-button edges. */
constexpr unsigned long kDebounceMilliseconds = 20;
/** @brief Maximum delay that groups two presses as a double click. */
constexpr unsigned long kDoubleClickMilliseconds = 300;

/** @brief Maximum number of catalog entries plus the two optional local controls. */
constexpr size_t kMaximumCarouselItems = kMaximumTelemetryDisplays + 2;
/** @brief Sentinel index used by carousel entries that have no MQTT telemetry value. */
constexpr size_t kNoTelemetryIndex = kMaximumTelemetryDisplays;
/** @brief Number of runtime tick labels around a non-percentage telemetry dial. */
constexpr size_t kDialTickLabelCount = 6;
/** @brief Horizontal positions of carousel preview, selected, and preview symbols. */
constexpr int kCarouselSymbolPositions[] = {-70, 0, 70};

/** @brief X coordinate of the fixed detail-dial center. */
constexpr float kDialCenterX = 120.0F;
/** @brief Y coordinate of the fixed detail-dial center. */
constexpr float kDialCenterY = 124.0F;
/** @brief Radius that keeps dynamic tick labels eight pixels clear of the dial ring. */
constexpr float kDialTickLabelRadius = 99.0F;
/** @brief Start angle, in radians, of the generated detail-dial tick labels. */
constexpr float kDialTickStartAngleRadians = 2.35619449F;
/** @brief Clockwise sweep, in radians, of the generated detail-dial tick labels. */
constexpr float kDialTickSweepRadians = 4.71238898F;

}  // namespace rv_control_ui::constants
