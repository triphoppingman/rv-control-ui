#include "mqtt_telemetry.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

namespace {

// The configured MQTT client buffer and accepted callback payload share this hard limit.
constexpr size_t kMaximumPayloadBytes = 1024;
constexpr uint32_t kReconnectIntervalMilliseconds = 5000;
// Association can take longer than an MQTT reconnect, especially after boot or AP roaming.
constexpr uint32_t kWiFiReconnectIntervalMilliseconds = 20000;

// A single application instance owns the PubSubClient callback required by its C-style API.
MqttTelemetry *activeTelemetry = nullptr;

// PubSubClient is used exclusively by the dedicated network task.
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

/** @brief Build the configured broker topic while tolerating accidental leading/trailing slashes. */
String composeTopic(const char *baseTopic, const char *suffix) {
  String topic(baseTopic);
  while (topic.endsWith("/")) topic.remove(topic.length() - 1);
  String trimmedSuffix(suffix);
  while (trimmedSuffix.startsWith("/")) trimmedSuffix.remove(0, 1);
  return topic + "/" + trimmedSuffix;
}

/** @brief Populate every optional value as unavailable before the first broker message arrives. */
TelemetrySnapshot emptySnapshot() {
  return {};
}

}  // namespace

/**
 * @brief Store connection settings and launch the network task on its own core.
 *
 * The copied configuration keeps the task independent from stack-local startup
 * variables. The task never calls LVGL and can reconnect while the UI remains
 * interactive on the Arduino loop thread.
 */
void MqttTelemetry::begin(const AppConfig &config, const DisplayCatalog &catalog) {
  config_ = config;
  catalog_ = catalog;
  for (size_t index = 0; index < catalog_.sourceCount; ++index) {
    subscriptionTopics_[index] = composeTopic(config_.mqttBaseTopic, catalog_.sources[index].topic);
  }
  activeTelemetry = this;
  xTaskCreatePinnedToCore(taskEntry, "mqtt", 6144, this, 1, nullptr, 0);
}

/**
 * @brief Return the newest telemetry snapshot available to the UI loop.
 *
 * Snapshot storage is implemented in the task because the client library
 * callback has no user-data argument. This method is completed in the next
 * implementation pass with the configured client state.
 */
bool MqttTelemetry::copyLatestSnapshot(TelemetrySnapshot &snapshot) {
  portENTER_CRITICAL(&snapshotLock_);
  const bool available = hasSnapshot_;
  if (available) snapshot = snapshot_;
  portEXIT_CRITICAL(&snapshotLock_);
  return available;
}

/** @brief Enter the background task using the instance supplied to FreeRTOS. */
void MqttTelemetry::taskEntry(void *parameter) {
  static_cast<MqttTelemetry *>(parameter)->run();
}

/**
 * @brief Maintain Wi-Fi and the configured broker subscription indefinitely.
 *
 * No connection attempt is allowed to block the LVGL thread. Wi-Fi receives a
 * twenty-second association window before another begin() call, preventing a
 * repeated request from restarting an in-progress association. MQTT retries
 * remain shorter once Wi-Fi is connected.
 */
void MqttTelemetry::run() {
  WiFi.mode(WIFI_STA);
  mqttClient.setServer(config_.mqttHost, config_.mqttPort);
  mqttClient.setBufferSize(kMaximumPayloadBytes);
  mqttClient.setCallback(messageReceived);

  uint32_t lastWiFiAttempt = millis() - kWiFiReconnectIntervalMilliseconds;
  uint32_t lastMqttAttempt = 0;
  bool wifiWasConnected = false;
  while (true) {
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiWasConnected) {
        wifiWasConnected = false;
        Serial.printf("[WARN] Wi-Fi disconnected; status=%d\n", WiFi.status());
      }
      if (now - lastWiFiAttempt >= kWiFiReconnectIntervalMilliseconds) {
        lastWiFiAttempt = now;
        Serial.printf("[INFO] Connecting to Wi-Fi; status=%d\n", WiFi.status());
        WiFi.begin(config_.wifiSsid, config_.wifiPassword);
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.printf("[INFO] Wi-Fi connected; RSSI=%ddBm IP=%s\n", WiFi.RSSI(), WiFi.localIP().toString().c_str());
    }

    if (!mqttClient.connected() && now - lastMqttAttempt >= kReconnectIntervalMilliseconds) {
      lastMqttAttempt = now;
      String clientId = "rv-control-ui-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
      const bool connected = config_.mqttUsername[0] == '\0'
                    ? mqttClient.connect(clientId.c_str())
                    : mqttClient.connect(clientId.c_str(), config_.mqttUsername, config_.mqttPassword);
      if (!connected) {
        Serial.printf("[WARN] MQTT connection failed; state=%d\n", mqttClient.state());
      } else {
        bool subscribed = true;
        for (size_t index = 0; index < catalog_.sourceCount; ++index) {
          if (!mqttClient.subscribe(subscriptionTopics_[index].c_str())) subscribed = false;
        }
        if (subscribed) Serial.printf("[INFO] MQTT subscribed to %u catalog source topics\n", static_cast<unsigned>(catalog_.sourceCount));
        else {
          Serial.println("[WARN] MQTT subscription failed");
          mqttClient.disconnect();
        }
      }
    }
    if (mqttClient.connected()) mqttClient.loop();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

/**
 * @brief Forward the PubSubClient callback to the active application instance.
 *
 * PubSubClient has no per-instance callback context. The firmware deliberately
 * has one MQTT telemetry owner, so this forwarding point is safe and concise.
 */
void MqttTelemetry::messageReceived(char *topic, uint8_t *payload, unsigned int length) {
  if (activeTelemetry) activeTelemetry->processMessage(topic, payload, length);
}

/**
 * @brief Parse and publish a complete catalog-defined JSON object received from MQTT.
 *
 * Oversized, unexpected-topic, malformed, and non-object messages are safely
 * discarded. A valid message can omit individual fields because catalog sources
 * can expose different telemetry sets.
 */
void MqttTelemetry::processMessage(const char *topic, const uint8_t *payload, size_t length) {
  size_t sourceIndex = catalog_.sourceCount;
  for (size_t index = 0; index < catalog_.sourceCount; ++index) {
    if (subscriptionTopics_[index] == topic) {
      sourceIndex = index;
      break;
    }
  }
  if (sourceIndex == catalog_.sourceCount) return;
  if (length == 0 || length > kMaximumPayloadBytes) {
    Serial.printf("[WARN] Ignoring MQTT payload with invalid size: %u\n", static_cast<unsigned>(length));
    return;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, payload, length);
  if (error || !document.is<JsonObjectConst>()) {
    Serial.println("[WARN] Ignoring malformed MQTT JSON payload");
    return;
  }

  const JsonObjectConst object = document.as<JsonObjectConst>();
  TelemetrySnapshot next = emptySnapshot();
  portENTER_CRITICAL(&snapshotLock_);
  next = snapshot_;
  portEXIT_CRITICAL(&snapshotLock_);
  for (size_t index = 0; index < catalog_.itemCount; ++index) {
    if (strcmp(catalog_.items[index].sourceId, catalog_.sources[sourceIndex].id) == 0) {
      next.values[index] = readValue(object, catalog_.items[index].valueKey);
    }
  }
  next.receivedAtMilliseconds = millis();

  portENTER_CRITICAL(&snapshotLock_);
  next.sequence = snapshot_.sequence + 1;
  snapshot_ = next;
  hasSnapshot_ = true;
  portEXIT_CRITICAL(&snapshotLock_);
}

/**
 * @brief Extract one numeric value from a JSON object without coercing invalid data.
 *
 * @return An available value only when the property exists, can convert to a
 * float, and is finite. Nulls, strings, and malformed numeric input remain
 * unavailable for the UI to display as `--`.
 */
TelemetryValue MqttTelemetry::readValue(const ArduinoJson::JsonObjectConst &object, const char *name) {
  const JsonVariantConst field = object[name];
  if (field.isNull() || !field.is<float>()) return {false, 0.0F};
  const float value = field.as<float>();
  return {isfinite(value), value};
}