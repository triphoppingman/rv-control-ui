#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config_loader.h"
#include "display_catalog.h"

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
  TelemetryValue values[kMaximumTelemetryDisplays];
  uint32_t receivedAtMilliseconds;
  uint32_t sequence;
};

/**
 * @brief Owns the Wi-Fi/MQTT FreeRTOS task and its latest catalog-backed snapshot.
 *
 * The task is deliberately independent from LVGL. It manages connection and
 * subscription state, parses valid bounded messages, and exposes copies of the
 * latest snapshot for the main loop to render.
 */
class MqttTelemetry {
 public:
  /** @brief Start the background connection and subscription task using validated settings. */
  void begin(const AppConfig &config, const DisplayCatalog &catalog);

  /** @brief Copy the latest parsed snapshot and return false until one is available. */
  bool copyLatestSnapshot(TelemetrySnapshot &snapshot);

 private:
  /** @brief FreeRTOS task entry point that forwards to the owning instance. */
  static void taskEntry(void *parameter);

  /** @brief Maintain Wi-Fi and MQTT connections without blocking the LVGL loop. */
  void run();

  /** @brief Receive one broker message and update the snapshot when it is valid JSON. */
  static void messageReceived(char *topic, uint8_t *payload, unsigned int length);

  /** @brief Parse one catalog-topic message and publish it to the main loop. */
  void processMessage(const char *topic, const uint8_t *payload, size_t length);

  /** @brief Copy one numeric JSON property when present and finite. */
  static TelemetryValue readValue(const ArduinoJson::JsonObjectConst &object, const char *name);

  // Task-owned connection settings copied at startup from the validated SPIFFS file.
  AppConfig config_ = {};

  // Catalog and normalized subscriptions are copied before the network task starts.
  DisplayCatalog catalog_ = {};
  String subscriptionTopics_[kMaximumTelemetrySources];

  // A short critical section protects cross-task copies of the latest telemetry state.
  portMUX_TYPE snapshotLock_ = portMUX_INITIALIZER_UNLOCKED;
  TelemetrySnapshot snapshot_ = {};
  bool hasSnapshot_ = false;
};