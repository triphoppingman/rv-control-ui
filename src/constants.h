#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Compile-time board, UI geometry, and network constants for RV Control UI.
 *
 * These values describe the Elecrow CrowPanel Advance 1.28-inch rotary
 * display, the fixed SquareLine layout, and the network/hotspot/API behavior
 * used by the firmware. Keep hardware pin assignments aligned with the
 * known-good Elecrow RotaryScreen reference. All compile-time constants live
 * here so the feature classes contain no magic numbers or path strings.
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

/** @brief NeoPixel (WS2812) status-LED data pin. */
constexpr int kStatusLedPin = 48;
/** @brief Number of NeoPixel pixels driven; the strip is illuminated as a whole. */
constexpr size_t kStatusLedCount = 6;
/** @brief Dim NeoPixel brightness so the indicator is visible but not glaring at night. */
constexpr uint8_t kStatusLedBrightness = 32;
/** @brief How long the Connected (success) state stays lit before the strip turns off. */
constexpr uint32_t kStatusLedConnectedOffMilliseconds = 10000;

/** @brief Initial user-selected display brightness percentage. */
constexpr uint8_t kDefaultBrightnessPercent = 50;
/** @brief Minimum elapsed time between accepted encoder-button edges. */
constexpr unsigned long kDebounceMilliseconds = 20;
/** @brief Maximum delay that groups two presses as a double click. */
constexpr unsigned long kDoubleClickMilliseconds = 300;

/** @brief Maximum MQTT-backed display definitions accepted from the configuration catalog. */
constexpr size_t kMaximumTelemetryDisplays = 16;
/** @brief Maximum MQTT source topics accepted from the configuration catalog. */
constexpr size_t kMaximumTelemetrySources = 8;
/** @brief Maximum visual palettes accepted from the configuration catalog. */
constexpr size_t kMaximumTelemetryPalettes = 8;

/** @brief Maximum number of catalog entries plus the two optional local controls. */
constexpr size_t kMaximumCarouselItems = kMaximumTelemetryDisplays + 2;
/** @brief Sentinel index used by carousel entries that have no MQTT telemetry value. */
constexpr size_t kNoTelemetryIndex = kMaximumTelemetryDisplays;
/** @brief Number of runtime tick labels around a non-percentage telemetry dial. */
constexpr size_t kDialTickLabelCount = 6;
/** @brief Horizontal positions of carousel preview, selected, and preview symbols. */
constexpr int kCarouselSymbolPositions[] = {-70, 0, 70};

/** @brief SPIFFS path of the unified JSON configuration. */
constexpr char kConfigFilePath[] = "/config.json";
/** @brief SPIFFS path of the temporary file used for atomic configuration writes. */
constexpr char kConfigTempFilePath[] = "/config.json.new";
/** @brief Hard limit on the unified configuration file and POST body size. */
constexpr size_t kMaximumConfigBytes = 12288;

/** @brief Default MQTT broker port when the configuration omits it. */
constexpr uint16_t kDefaultMqttPort = 1883;
/** @brief Default expected telemetry publish cadence in seconds. */
constexpr uint16_t kDefaultPollIntervalSeconds = 60;
/** @brief Default backlight inactivity timeout in seconds. */
constexpr uint16_t kDefaultSleepAfterSeconds = 300;
/** @brief Default temperature unit when the configuration omits it. */
constexpr char kDefaultTemperatureUnit[] = "F";
/** @brief Default serial logging level when the configuration omits it. */
constexpr char kDefaultSerialLevel[] = "INFO";

/** @brief TCP port the configuration HTTP API listens on. */
constexpr uint16_t kConfigApiPort = 80;
/** @brief Grace delay after a successful config POST before the device restarts. */
constexpr uint32_t kConfigApiRestartDelayMilliseconds = 500;

/** @brief Hotspot SSID prefix; the last four MAC hex digits are appended. */
constexpr char kHotspotSsidPrefix[] = "rv-control-ui-";
/** @brief Fixed hotspot password (a documented, non-secret setup value). */
constexpr char kHotspotPassword[] = "Password123!";
/** @brief Static hotspot IP address (first octet). */
constexpr uint8_t kHotspotAddressOctet0 = 192;
/** @brief Static hotspot IP address (second octet). */
constexpr uint8_t kHotspotAddressOctet1 = 168;
/** @brief Static hotspot IP address (third octet). */
constexpr uint8_t kHotspotAddressOctet2 = 77;
/** @brief Static hotspot IP address (fourth octet). */
constexpr uint8_t kHotspotAddressOctet3 = 1;

/** @brief Length of one Wi-Fi association attempt window before it is scored a failure. */
constexpr uint32_t kWiFiAssociationCycleMilliseconds = 30000;
/** @brief Consecutive failed association cycles that trigger hotspot fallback. */
constexpr uint8_t kWiFiFailedCyclesBeforeHotspot = 2;

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

