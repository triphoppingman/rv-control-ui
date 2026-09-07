#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "constants.h"

/**
 * @brief Typed, bounded device settings loaded from the unified /config.json.
 *
 * Fixed-size character arrays avoid unbounded allocation from configuration
 * input. Password values are retained only for Wi-Fi/MQTT connection setup and
 * must never be sent to the serial logger. An empty wifiSsid is valid and
 * selects hotspot-only operation.
 */
struct AppConfig {
  /** @brief Return the one application-wide configuration populated at boot. */
  static AppConfig &instance();

  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[128];
  uint16_t mqttPort;
  char mqttUsername[65];
  char mqttPassword[65];
  char mqttBaseTopic[64];
  char temperatureUnit[4];
  uint16_t expectedPollIntervalSeconds;
  uint16_t sleepAfterSeconds;
  bool showBrightness;
  bool showWifi;
  char serialLevel[8];
};

/** @brief One catalog-defined telemetry source and its MQTT topic suffix. */
struct TelemetrySourceDefinition {
  char id[32];
  char topic[64];
};

/** @brief One named set of telemetry detail tick-label and background colors. */
struct TelemetryPaletteDefinition {
  char id[32];
  uint32_t tickLabelColor;
  uint32_t displayBackground;
};

/**
 * @brief One read-only MQTT display definition from the configuration catalog.
 *
 * The source/key pair identifies the JSON value, while text and presentation
 * values define how the existing SquareLine carousel renders the measurement.
 */
struct TelemetryDisplayDefinition {
  char title[32];
  char carouselTitle[32];
  char sourceId[32];
  char paletteId[32];
  char valueKey[40];
  char unit[8];
  char icon[16];
  char screen[16];
  int arcMinimum;
  int arcMaximum;
  uint32_t tickLabelColor;
  uint32_t displayBackground;
  uint8_t precision;
  bool compact;
  uint8_t fontSize;
};

/** @brief Fixed-capacity list of MQTT-backed definitions parsed at startup. */
struct DisplayCatalog {
  /** @brief Return the one application-wide catalog populated at boot. */
  static DisplayCatalog &instance();

  TelemetrySourceDefinition sources[rv_control_ui::constants::kMaximumTelemetrySources];
  size_t sourceCount;
  TelemetryPaletteDefinition palettes[rv_control_ui::constants::kMaximumTelemetryPalettes];
  size_t paletteCount;
  TelemetryDisplayDefinition items[rv_control_ui::constants::kMaximumTelemetryDisplays];
  size_t itemCount;
};

/**
 * @brief Loads and validates the unified /config.json device configuration.
 *
 * The store owns SPIFFS mounting and the bounded JSON document. It produces
 * the typed AppConfig and DisplayCatalog consumed by the rest of the firmware,
 * and exposes a shared document validator so the boot path and the HTTP API's
 * POST path apply exactly the same rules. The whole document is validated and
 * rejected as a unit; no partially applied configuration is produced.
 */
class ConfigStore {
 public:
  /** @brief Return the one configuration-store service used by the firmware. */
  static ConfigStore &instance();

  /**
   * @brief Mount SPIFFS and load the validated /config.json file.
   *
   * @param config Receives the typed settings when the configuration is valid.
   * @param catalog Receives the typed display catalog when the configuration is valid.
   * @param error Receives a concise diagnostic safe for serial output on failure.
   * @param errorSize Number of bytes available in error, including its terminator.
   * @return True when SPIFFS and the whole configuration document are valid.
   */
  bool load(AppConfig &config, DisplayCatalog &catalog, char *error, size_t errorSize);

  /**
   * @brief Validate an already-parsed JSON document against the full schema.
   *
   * Used by the HTTP API to validate a POSTed configuration with the same
   * rules applied at boot. On success the typed outputs are populated.
   *
   * @param document The parsed JSON document to validate.
   * @param config Receives the typed settings when valid.
   * @param catalog Receives the typed display catalog when valid.
   * @param error Receives a concise diagnostic on failure.
   * @param errorSize Number of bytes available in error, including its terminator.
   * @return True when the document satisfies the entire schema.
   */
  bool validateDocument(const ArduinoJson::JsonDocument &document, AppConfig &config,
                        DisplayCatalog &catalog, char *error, size_t errorSize);

  /**
   * @brief Serialize a typed configuration to its JSON document form.
   *
   * Used by the HTTP API to return the running configuration. The output
   * includes secrets exactly as stored; nothing is redacted.
   *
   * @param config The typed settings to serialize.
   * @param catalog The typed display catalog to serialize.
   * @param document Receives the JSON representation.
   */
  void toJson(const AppConfig &config, const DisplayCatalog &catalog, ArduinoJson::JsonDocument &document);

 private:
  /** @brief Construct the singleton; use instance() to access configuration storage. */
  ConfigStore() = default;
};
