#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Typed, bounded settings loaded from the device's SPIFFS config.ini.
 *
 * Fixed-size character arrays avoid unbounded allocation from configuration
 * input. Password values are retained only for later Wi-Fi/MQTT connection
 * setup and must never be sent to the serial logger.
 */
struct AppConfig {
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

/**
 * @brief Mount SPIFFS and load one validated /config.ini file.
 *
 * @param config Receives the typed settings when the complete configuration is valid.
 * @param error Receives a concise diagnostic safe for serial output on failure.
 * @param errorSize Number of bytes available in error, including its terminator.
 * @return True when SPIFFS and every required configuration setting are valid.
 */
bool loadAppConfig(AppConfig &config, char *error, size_t errorSize);