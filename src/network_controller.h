#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config_api.h"
#include "config_loader.h"
#include "constants.h"
#include "status_led.h"

/**
 * @brief One optional numeric field from a catalog-defined MQTT payload.
 *
 * Payload fields vary by collector source. Keeping a presence flag
 * permits the UI to render missing values as `--` without discarding an
 * otherwise useful telemetry snapshot.
 */
struct TelemetryValue {
  bool available;
  float value;
};

/**
 * @brief Latest numeric values for each catalog-defined display item.
 *
 * The timestamp is recorded when a complete valid MQTT message is received,
 * allowing loop() to calculate freshness without any LVGL access from the
 * networking task.
 */
struct TelemetrySnapshot {
  TelemetryValue values[rv_control_ui::constants::kMaximumTelemetryDisplays];
  uint32_t receivedAtMilliseconds;
  uint32_t sequence;
};

/**
 * @brief Owns all networking: Wi-Fi station/hotspot, MQTT, the config API, and the status LED.
 *
 * NetworkController runs the dedicated network FreeRTOS task. It manages the
 * Wi-Fi station connection with bounded reconnects, falls back to a setup
 * hotspot when no station network is configured or association repeatedly
 * fails, keeps the configuration REST API available in every mode, runs the
 * MQTT subscription, and drives the NeoPixel status LED. It never touches
 * LVGL; loop() consumes snapshots copied out of this task.
 */
class NetworkController {
 public:
  /** @brief Start the background network task using validated settings and catalog. */
  void begin(const AppConfig &config, const DisplayCatalog &catalog, ConfigStore &store);

  /** @brief Copy the latest parsed snapshot and return false until one is available. */
  bool copyLatestSnapshot(TelemetrySnapshot &snapshot);

 private:
  /** @brief FreeRTOS task entry point that forwards to the owning instance. */
  static void taskEntry(void *parameter);

  /** @brief Maintain Wi-Fi, MQTT, the API, and the status LED without blocking the LVGL loop. */
  void run();

  /** @brief Bring up the setup hotspot and mark the status LED. */
  void enterHotspot(const char *reason);

  /** @brief Tear down the hotspot after a successful station association. */
  void exitHotspot();

  /** @brief Refresh the status LED from the current Wi-Fi/MQTT/hotspot state. */
  void updateStatusLed();

  /** @brief Push the live network state to the API's info-endpoint snapshot. */
  void publishNetworkStatus();

  /** @brief Receive one broker message and update the snapshot when it is valid JSON. */
  static void messageReceived(char *topic, uint8_t *payload, unsigned int length);

  /** @brief Parse one catalog-topic message and publish it to the main loop. */
  void processMessage(const char *topic, const uint8_t *payload, size_t length);

  /** @brief Copy one numeric JSON property when present and finite. */
  static TelemetryValue readValue(const ArduinoJson::JsonObjectConst &object, const char *name);

  // Task-owned connection settings copied at startup from the validated config file.
  AppConfig config_ = {};

  // Catalog and normalized subscriptions are copied before the network task starts.
  DisplayCatalog catalog_ = {};
  String subscriptionTopics_[rv_control_ui::constants::kMaximumTelemetrySources];

  // The configuration API and status LED are owned by this task, never by LVGL.
  ConfigStore *store_ = nullptr;
  ConfigApi api_;
  StatusLed statusLed_;

  // Hotspot and station-state tracking for the fallback and status LED.
  bool hotspotActive_ = false;
  bool stationConfigured_ = false;
  bool wifiWasConnected_ = false;
  bool mqttWasConnected_ = false;
  uint8_t failedAssociationCycles_ = 0;
  uint32_t associationStartedAtMilliseconds_ = 0;
  uint32_t lastMqttAttempt_ = 0;

  // A short critical section protects cross-task copies of the latest telemetry state.
  portMUX_TYPE snapshotLock_ = portMUX_INITIALIZER_UNLOCKED;
  TelemetrySnapshot snapshot_ = {};
  bool hasSnapshot_ = false;
};
