#include "network_controller.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <math.h>
#include <string.h>

namespace {

using namespace rv_control_ui::constants;

// The configured MQTT client buffer and accepted callback payload share this hard limit.
constexpr size_t kMaximumPayloadBytes = 1024;
constexpr uint32_t kReconnectIntervalMilliseconds = 5000;

// A single application instance owns the PubSubClient callback required by its C-style API.
NetworkController *activeNetworkController = nullptr;

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

}  // namespace

/**
 * @brief Store connection settings, start the API and status LED, and launch the task.
 *
 * The copied configuration keeps the task independent from stack-local startup
 * variables. The task never calls LVGL and can reconnect while the UI remains
 * interactive on the Arduino loop thread.
 */
void NetworkController::begin(const AppConfig &config, const DisplayCatalog &catalog, ConfigStore &store) {
  config_ = config;
  catalog_ = catalog;
  store_ = &store;
  stationConfigured_ = config_.wifiSsid[0] != '\0';
  for (size_t index = 0; index < catalog_.sourceCount; ++index) {
    subscriptionTopics_[index] = composeTopic(config_.mqttBaseTopic, catalog_.sources[index].topic);
  }
  activeNetworkController = this;
  // The status LED and HTTP API are started inside run(), after WiFi.mode() has
  // brought up lwIP. Starting the WebServer here would create a socket before the
  // TCP/IP stack exists and crash on a null lwIP queue. The stack is sized for the
  // WebServer plus the JSON POST parse, which run on this task.
  xTaskCreatePinnedToCore(taskEntry, "network", 16384, this, 1, nullptr, 0);
}

/**
 * @brief Return the newest telemetry snapshot available to the UI loop.
 *
 * A short critical section guards the cross-task copy so loop() always reads
 * a complete, consistent snapshot even while the network task is updating it.
 */
bool NetworkController::copyLatestSnapshot(TelemetrySnapshot &snapshot) {
  portENTER_CRITICAL(&snapshotLock_);
  const bool available = hasSnapshot_;
  if (available) snapshot = snapshot_;
  portEXIT_CRITICAL(&snapshotLock_);
  return available;
}

/** @brief Enter the background task using the instance supplied to FreeRTOS. */
void NetworkController::taskEntry(void *parameter) {
  static_cast<NetworkController *>(parameter)->run();
}

/**
 * @brief Bring up the setup hotspot and mark the status LED.
 *
 * The hotspot uses a MAC-suffixed SSID and a static 192.168.77.1 address so a
 * client can always find the configuration API. MQTT stays off while active.
 */
void NetworkController::enterHotspot(const char *reason) {
  if (hotspotActive_) return;
  hotspotActive_ = true;
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s%04X", kHotspotSsidPrefix,
           static_cast<unsigned int>(ESP.getEfuseMac() & 0xFFFF));
  const IPAddress address(kHotspotAddressOctet0, kHotspotAddressOctet1, kHotspotAddressOctet2, kHotspotAddressOctet3);
  const IPAddress gateway(kHotspotAddressOctet0, kHotspotAddressOctet1, kHotspotAddressOctet2, kHotspotAddressOctet3);
  const IPAddress netmask(255, 255, 255, 0);
  // Stop the station driver's auto-reconnect churn and disable power save so the
  // access point stays up while clients are attached; background station retries
  // are driven explicitly on the slow cycle below.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(address, gateway, netmask);
  WiFi.softAP(ssid, kHotspotPassword);
  Serial.printf("[WARN] Entering setup hotspot (%s); SSID=%s IP=%s\n", reason, ssid,
                WiFi.softAPIP().toString().c_str());
  updateStatusLed();
}

/** @brief Tear down the hotspot once the station association succeeds. */
void NetworkController::exitHotspot() {
  if (!hotspotActive_) return;
  hotspotActive_ = false;
  WiFi.softAPdisconnect(true);
  WiFi.setSleep(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[INFO] Exited setup hotspot; station connected");
  updateStatusLed();
}

/** @brief Refresh the status LED from the current hotspot/Wi-Fi/MQTT state. */
void NetworkController::updateStatusLed() {
  if (hotspotActive_) {
    statusLed_.show(StatusLedState::Hotspot);
  } else if (WiFi.status() != WL_CONNECTED) {
    statusLed_.show(StatusLedState::Starting);
  } else if (!mqttClient.connected()) {
    statusLed_.show(StatusLedState::BrokerDown);
  } else {
    statusLed_.show(StatusLedState::Connected);
  }
}

/** @brief Push the live network state to the API's info-endpoint snapshot. */
void NetworkController::publishNetworkStatus() {
  NetworkStatus status = {};
  status.hotspotActive = hotspotActive_;
  status.wifiConnected = WiFi.status() == WL_CONNECTED;
  status.mqttConnected = mqttClient.connected();
  if (hotspotActive_) {
    strlcpy(status.wifiSsid, WiFi.softAPSSID().c_str(), sizeof(status.wifiSsid));
    strlcpy(status.ipAddress, WiFi.softAPIP().toString().c_str(), sizeof(status.ipAddress));
    status.rssi = 0;
  } else if (status.wifiConnected) {
    strlcpy(status.wifiSsid, WiFi.SSID().c_str(), sizeof(status.wifiSsid));
    strlcpy(status.ipAddress, WiFi.localIP().toString().c_str(), sizeof(status.ipAddress));
    status.rssi = WiFi.RSSI();
  }
  api_.setNetworkStatus(status);
}

/**
 * @brief Maintain Wi-Fi (station or hotspot), the API, and the MQTT subscription.
 *
 * No connection attempt is allowed to block the LVGL thread. Station
 * association runs in bounded 30-second cycles; repeated failures fall back to
 * the setup hotspot while station retries continue in the background. The
 * configuration API is serviced in every mode so the device is always
 * reconfigurable without reprogramming.
 */
void NetworkController::run() {
  // WiFi.mode() initializes lwIP; only after it returns may any code open sockets.
  WiFi.mode(WIFI_STA);
  mqttClient.setServer(config_.mqttHost, config_.mqttPort);
  mqttClient.setBufferSize(kMaximumPayloadBytes);
  mqttClient.setCallback(messageReceived);

  // Start the status LED and the always-on configuration API now that lwIP is up.
  statusLed_.begin();
  api_.begin(*store_, config_, catalog_);

  if (!stationConfigured_) {
    enterHotspot("no station network configured");
  } else {
    Serial.printf("[INFO] Connecting to Wi-Fi\n");
    WiFi.begin(config_.wifiSsid, config_.wifiPassword);
    associationStartedAtMilliseconds_ = millis();
    updateStatusLed();
  }

  while (true) {
    const uint32_t now = millis();
    api_.handleClient();
    api_.serviceRestart();
    statusLed_.update();
    publishNetworkStatus();

    if (WiFi.status() != WL_CONNECTED) {
      if (wifiWasConnected_) {
        wifiWasConnected_ = false;
        mqttWasConnected_ = false;
        Serial.printf("[WARN] Wi-Fi disconnected; status=%d\n", WiFi.status());
        if (stationConfigured_) {
          WiFi.begin(config_.wifiSsid, config_.wifiPassword);
          associationStartedAtMilliseconds_ = now;
        }
        updateStatusLed();
      }
      if (stationConfigured_ && now - associationStartedAtMilliseconds_ >= kWiFiAssociationCycleMilliseconds) {
        ++failedAssociationCycles_;
        Serial.printf("[WARN] Wi-Fi association cycle %u failed; status=%d\n",
                      static_cast<unsigned>(failedAssociationCycles_), WiFi.status());
        if (!hotspotActive_ && failedAssociationCycles_ >= kWiFiFailedCyclesBeforeHotspot) {
          enterHotspot("station association failed");
        }
        // While the hotspot is active, retry the station on the slow cycle with a
        // single begin() and no auto-reconnect churn, so the AP stays up for clients.
        WiFi.begin(config_.wifiSsid, config_.wifiPassword);
        associationStartedAtMilliseconds_ = now;
        updateStatusLed();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Station is associated: recover from any hotspot fallback and run MQTT.
    exitHotspot();
    failedAssociationCycles_ = 0;
    if (!wifiWasConnected_) {
      wifiWasConnected_ = true;
      Serial.printf("[INFO] Wi-Fi connected; RSSI=%ddBm IP=%s\n", WiFi.RSSI(), WiFi.localIP().toString().c_str());
      updateStatusLed();
    }

    if (!mqttClient.connected() && now - lastMqttAttempt_ >= kReconnectIntervalMilliseconds) {
      lastMqttAttempt_ = now;
      String clientId = "rv-control-ui-" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
      const bool connected = config_.mqttUsername[0] == '\0'
                    ? mqttClient.connect(clientId.c_str())
                    : mqttClient.connect(clientId.c_str(), config_.mqttUsername, config_.mqttPassword);
      if (!connected) {
        Serial.printf("[WARN] MQTT connection failed; state=%d\n", mqttClient.state());
        if (mqttWasConnected_) {
          mqttWasConnected_ = false;
          updateStatusLed();
        }
      } else {
        bool subscribed = true;
        for (size_t index = 0; index < catalog_.sourceCount; ++index) {
          if (!mqttClient.subscribe(subscriptionTopics_[index].c_str())) subscribed = false;
        }
        if (subscribed) {
          Serial.printf("[INFO] MQTT subscribed to %u catalog source topics\n", static_cast<unsigned>(catalog_.sourceCount));
          if (!mqttWasConnected_) {
            mqttWasConnected_ = true;
            updateStatusLed();
          }
        } else {
          Serial.println("[WARN] MQTT subscription failed");
          mqttClient.disconnect();
          if (mqttWasConnected_) {
            mqttWasConnected_ = false;
            updateStatusLed();
          }
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
 * has one network owner, so this forwarding point is safe and concise.
 */
void NetworkController::messageReceived(char *topic, uint8_t *payload, unsigned int length) {
  if (activeNetworkController) activeNetworkController->processMessage(topic, payload, length);
}

/**
 * @brief Parse and publish a complete catalog-defined JSON object received from MQTT.
 *
 * Oversized, unexpected-topic, malformed, and non-object messages are safely
 * discarded. A valid message can omit individual fields because catalog sources
 * can expose different telemetry sets.
 */
void NetworkController::processMessage(const char *topic, const uint8_t *payload, size_t length) {
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
  TelemetrySnapshot next = {};
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
TelemetryValue NetworkController::readValue(const ArduinoJson::JsonObjectConst &object, const char *name) {
  const JsonVariantConst field = object[name];
  if (field.isNull() || !field.is<float>()) return {false, 0.0F};
  const float value = field.as<float>();
  return {isfinite(value), value};
}
