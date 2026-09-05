#include "config_loader.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>

namespace {

// A configuration file needs only a few hundred bytes; this cap rejects bad uploads safely.
constexpr size_t kMaximumConfigBytes = 4096;

/** @brief Store a safe diagnostic without overrunning the caller's error buffer. */
void setError(char *error, size_t errorSize, const char *message) {
  if (errorSize > 0) strlcpy(error, message, errorSize);
}

/**
 * @brief Copy an INI value into a bounded configuration field.
 *
 * Values that do not fit are rejected rather than silently truncated, which
 * avoids connecting to a broker or network other than the one configured.
 */
bool copyValue(char *destination, size_t destinationSize, const String &value) {
  if (value.length() >= destinationSize) return false;
  strlcpy(destination, value.c_str(), destinationSize);
  return true;
}

/** @brief Parse a display visibility value that must be exactly zero or one. */
bool copyVisibilityValue(bool &destination, const String &value) {
  if (value == "0") {
    destination = false;
    return true;
  }
  if (value == "1") {
    destination = true;
    return true;
  }
  return false;
}

/**
 * @brief Assign one recognized section/key pair to the typed configuration.
 *
 * @return True for a known key whose value fits its destination field.
 */
bool applyValue(AppConfig &config, const String &section, const String &key, const String &value) {
  if (section == "wifi" && key == "ssid") return copyValue(config.wifiSsid, sizeof(config.wifiSsid), value);
  if (section == "wifi" && key == "password") return copyValue(config.wifiPassword, sizeof(config.wifiPassword), value);
  if (section == "mqtt" && key == "host") return copyValue(config.mqttHost, sizeof(config.mqttHost), value);
  if (section == "mqtt" && key == "username") return copyValue(config.mqttUsername, sizeof(config.mqttUsername), value);
  if (section == "mqtt" && key == "password") return copyValue(config.mqttPassword, sizeof(config.mqttPassword), value);
  if (section == "mqtt" && key == "base_topic") return copyValue(config.mqttBaseTopic, sizeof(config.mqttBaseTopic), value);
  // Topic suffixes now belong exclusively to display-catalog.json; accept prior
  // settings during migration so an installed device remains bootable.
  if (section == "mqtt" && (key == "renogy_topic" || key == "hughes_topic")) return true;
  if (section == "display" && key == "temperature_unit") return copyValue(config.temperatureUnit, sizeof(config.temperatureUnit), value);
  if (section == "display" && key == "show_brightness") return copyVisibilityValue(config.showBrightness, value);
  if (section == "display" && key == "show_wifi") return copyVisibilityValue(config.showWifi, value);
  if (section == "logging" && key == "serial_level") return copyValue(config.serialLevel, sizeof(config.serialLevel), value);
  if (section == "mqtt" && key == "port") {
    const long port = value.toInt();
    if (port < 1 || port > 65535) return false;
    config.mqttPort = static_cast<uint16_t>(port);
    return true;
  }
  if (section == "display" && key == "expected_poll_interval_seconds") {
    const long interval = value.toInt();
    if (interval < 1 || interval > 65535) return false;
    config.expectedPollIntervalSeconds = static_cast<uint16_t>(interval);
    return true;
  }
  if (section == "display" && key == "sleep_after_seconds") {
    const long seconds = value.toInt();
    if (seconds < 0 || seconds > 65535) return false;
    config.sleepAfterSeconds = static_cast<uint16_t>(seconds);
    return true;
  }
  return false;
}

/** @brief Ensure values required for later Wi-Fi and MQTT setup were supplied. */
bool validateConfig(const AppConfig &config, char *error, size_t errorSize) {
  if (config.wifiSsid[0] == '\0' || config.mqttHost[0] == '\0' || config.mqttBaseTopic[0] == '\0') {
    setError(error, errorSize, "config.ini is missing a required setting");
    return false;
  }
  return true;
}

}  // namespace

bool loadAppConfig(AppConfig &config, char *error, size_t errorSize) {
  memset(&config, 0, sizeof(config));
  config.mqttPort = 1883;
  config.expectedPollIntervalSeconds = 60;
  config.sleepAfterSeconds = 300;
  config.showBrightness = true;
  config.showWifi = true;
  strlcpy(config.temperatureUnit, "F", sizeof(config.temperatureUnit));
  strlcpy(config.serialLevel, "INFO", sizeof(config.serialLevel));

  if (!SPIFFS.begin(false)) {
    setError(error, errorSize, "SPIFFS mount failed");
    return false;
  }

  File file = SPIFFS.open("/config.ini", FILE_READ);
  if (!file) {
    setError(error, errorSize, "/config.ini is missing");
    return false;
  }
  if (file.size() == 0 || file.size() > kMaximumConfigBytes) {
    file.close();
    setError(error, errorSize, "/config.ini has an invalid size");
    return false;
  }

  String section;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) continue;
    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.substring(1, line.length() - 1);
      section.trim();
      continue;
    }

    const int separator = line.indexOf('=');
    if (section.isEmpty() || separator < 1) {
      file.close();
      setError(error, errorSize, "config.ini contains malformed INI syntax");
      return false;
    }
    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();
    if (!applyValue(config, section, key, value)) {
      file.close();
      setError(error, errorSize, "config.ini contains an invalid setting");
      return false;
    }
  }
  file.close();
  return validateConfig(config, error, errorSize);
}