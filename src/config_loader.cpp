#include "config_loader.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

using rv_control_ui::constants::kConfigFilePath;
using rv_control_ui::constants::kDefaultMqttPort;
using rv_control_ui::constants::kDefaultPollIntervalSeconds;
using rv_control_ui::constants::kDefaultSerialLevel;
using rv_control_ui::constants::kDefaultSleepAfterSeconds;
using rv_control_ui::constants::kDefaultTemperatureUnit;
using rv_control_ui::constants::kMaximumConfigBytes;
using rv_control_ui::constants::kMaximumTelemetryDisplays;
using rv_control_ui::constants::kMaximumTelemetryPalettes;
using rv_control_ui::constants::kMaximumTelemetrySources;

/** @brief Store a safe diagnostic without overrunning the caller's error buffer. */
void setError(char *error, size_t errorSize, const char *message) {
  if (errorSize > 0) strlcpy(error, message, errorSize);
}

/** @brief Store a printf-formatted diagnostic bounded by the caller's error buffer. */
void setErrorf(char *error, size_t errorSize, const char *format, ...) {
  if (errorSize == 0) return;
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(error, errorSize, format, arguments);
  va_end(arguments);
}

/**
 * @brief Copy a JSON string into a bounded configuration field.
 *
 * Values that do not fit are rejected rather than silently truncated, which
 * avoids connecting to a broker or network other than the one configured.
 */
bool copyValue(char *destination, size_t destinationSize, JsonVariantConst value) {
  if (!value.is<const char *>()) return false;
  const char *text = value.as<const char *>();
  if (strlen(text) >= destinationSize) return false;
  strlcpy(destination, text, destinationSize);
  return true;
}

/** @brief Copy a JSON string, tolerating an absent value as an empty field. */
bool copyOptionalValue(char *destination, size_t destinationSize, JsonVariantConst value) {
  if (value.isNull()) {
    destination[0] = '\0';
    return true;
  }
  return copyValue(destination, destinationSize, value);
}

/** @brief Read a bounded unsigned integer with a default when the key is absent. */
bool readUint16(JsonObjectConst object, const char *key, uint16_t minimum, uint16_t maximum, uint16_t fallback,
                uint16_t &destination) {
  destination = fallback;
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!value.is<long>()) return false;
  const long parsed = value.as<long>();
  if (parsed < minimum || parsed > maximum) return false;
  destination = static_cast<uint16_t>(parsed);
  return true;
}

/** @brief Read a boolean with a default when the key is absent. */
bool readBool(JsonObjectConst object, const char *key, bool fallback, bool &destination) {
  destination = fallback;
  const JsonVariantConst value = object[key];
  if (value.isNull()) return true;
  if (!value.is<bool>()) return false;
  destination = value.as<bool>();
  return true;
}

/** @brief Convert one hexadecimal RGB digit to its numeric value. */
int hexadecimalValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

/** @brief Parse an optional #RRGGBB catalog color or retain its display default. */
bool parseColor(JsonVariantConst value, uint32_t defaultColor, uint32_t &color) {
  color = defaultColor;
  if (value.isNull()) return true;
  if (!value.is<const char *>()) return false;
  const char *text = value.as<const char *>();
  if (strlen(text) != 7 || text[0] != '#') return false;
  uint32_t parsed = 0;
  for (size_t index = 1; index < 7; ++index) {
    const int digit = hexadecimalValue(text[index]);
    if (digit < 0) return false;
    parsed = (parsed << 4U) | static_cast<uint32_t>(digit);
  }
  color = parsed;
  return true;
}

/** @brief Return whether a catalog identifier character is portable and unambiguous. */
bool isIdentifierCharacter(char character) {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '_' || character == '-';
}

/** @brief Validate a stable identifier used by catalog sources, palettes, and items. */
bool isValidIdentifier(const char *id) {
  if (id[0] == '\0') return false;
  for (const char *character = id; *character != '\0'; ++character) {
    if (!isIdentifierCharacter(*character)) return false;
  }
  return true;
}

/** @brief Validate a relative MQTT topic suffix without wildcards or empty levels. */
bool isValidTopicSuffix(const char *topic) {
  if (topic[0] == '\0' || topic[0] == '/' || topic[strlen(topic) - 1] == '/') return false;
  char previous = '\0';
  for (const char *character = topic; *character != '\0'; ++character) {
    if (*character <= ' ' || *character == '#' || *character == '+' || (*character == '/' && previous == '/')) return false;
    previous = *character;
  }
  return true;
}

/** @brief Find a parsed catalog source by its stable identifier. */
size_t sourceIndex(const DisplayCatalog &catalog, const char *id) {
  for (size_t index = 0; index < catalog.sourceCount; ++index) {
    if (strcmp(catalog.sources[index].id, id) == 0) return index;
  }
  return kMaximumTelemetrySources;
}

/** @brief Find a parsed catalog palette by its stable identifier. */
size_t paletteIndex(const DisplayCatalog &catalog, const char *id) {
  for (size_t index = 0; index < catalog.paletteCount; ++index) {
    if (strcmp(catalog.palettes[index].id, id) == 0) return index;
  }
  return kMaximumTelemetryPalettes;
}

/** @brief Parse one bounded source definition and reject duplicate identifiers or topics. */
bool parseSource(JsonObjectConst source, const DisplayCatalog &catalog, TelemetrySourceDefinition &definition) {
  memset(&definition, 0, sizeof(definition));
  if (!copyValue(definition.id, sizeof(definition.id), source["id"]) ||
      !copyValue(definition.topic, sizeof(definition.topic), source["topic"]) || !isValidIdentifier(definition.id) ||
      !isValidTopicSuffix(definition.topic)) return false;
  for (size_t index = 0; index < catalog.sourceCount; ++index) {
    if (strcmp(catalog.sources[index].id, definition.id) == 0 || strcmp(catalog.sources[index].topic, definition.topic) == 0) return false;
  }
  return true;
}

/** @brief Parse one named visual palette and reject duplicate or malformed definitions. */
bool parsePalette(JsonObjectConst source, const DisplayCatalog &catalog, TelemetryPaletteDefinition &definition) {
  memset(&definition, 0, sizeof(definition));
  if (!copyValue(definition.id, sizeof(definition.id), source["id"]) || !isValidIdentifier(definition.id) ||
      !parseColor(source["tick_label_color"], 0xFFFFFF, definition.tickLabelColor) ||
      !parseColor(source["display_background"], 0x000000, definition.displayBackground)) return false;
  for (size_t index = 0; index < catalog.paletteCount; ++index) {
    if (strcmp(catalog.palettes[index].id, definition.id) == 0) return false;
  }
  return true;
}

/** @brief Parse one display definition bound to existing catalog source and palette IDs. */
bool parseItem(JsonObjectConst source, const DisplayCatalog &catalog, TelemetryDisplayDefinition &item) {
  memset(&item, 0, sizeof(item));
  if (!copyValue(item.title, sizeof(item.title), source["title"]) ||
      !copyValue(item.carouselTitle, sizeof(item.carouselTitle), source["carousel_title"]) ||
      !copyValue(item.sourceId, sizeof(item.sourceId), source["source"]) ||
      !copyValue(item.paletteId, sizeof(item.paletteId), source["palette"]) ||
      !copyValue(item.valueKey, sizeof(item.valueKey), source["value_key"]) ||
      !copyValue(item.unit, sizeof(item.unit), source["unit"]) ||
      !copyValue(item.icon, sizeof(item.icon), source["icon"]) ||
      !copyValue(item.screen, sizeof(item.screen), source["screen"])) return false;
  const size_t resolvedPaletteIndex = paletteIndex(catalog, item.paletteId);
  if (resolvedPaletteIndex == catalog.paletteCount) return false;
  item.tickLabelColor = catalog.palettes[resolvedPaletteIndex].tickLabelColor;
  item.displayBackground = catalog.palettes[resolvedPaletteIndex].displayBackground;
  item.arcMinimum = source["arc_min"] | 0;
  item.arcMaximum = source["arc_max"] | 100;
  item.precision = source["precision"] | 1;
  item.compact = source["compact"] | false;
  item.fontSize = source["font_size"] | 40;
  return item.arcMinimum >= -5000 && item.arcMinimum < item.arcMaximum && item.arcMaximum <= 5000 && item.precision <= 3 &&
         (item.fontSize == 40 || item.fontSize == 28 || item.fontSize == 20) &&
         sourceIndex(catalog, item.sourceId) < catalog.sourceCount;
}

/** @brief Apply schema defaults before any JSON values are read. */
void applyDefaults(AppConfig &config) {
  memset(&config, 0, sizeof(config));
  config.mqttPort = kDefaultMqttPort;
  config.expectedPollIntervalSeconds = kDefaultPollIntervalSeconds;
  config.sleepAfterSeconds = kDefaultSleepAfterSeconds;
  config.showBrightness = true;
  config.showWifi = true;
  strlcpy(config.temperatureUnit, kDefaultTemperatureUnit, sizeof(config.temperatureUnit));
  strlcpy(config.serialLevel, kDefaultSerialLevel, sizeof(config.serialLevel));
}

}  // namespace

bool ConfigStore::validateDocument(const ArduinoJson::JsonDocument &document, AppConfig &config,
                                   DisplayCatalog &catalog, char *error, size_t errorSize) {
  if (!document.is<JsonObjectConst>()) {
    setError(error, errorSize, "config root must be an object");
    return false;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();

  applyDefaults(config);
  memset(&catalog, 0, sizeof(catalog));

  // wifi.ssid may be empty (hotspot-only); every other string must fit its field.
  const JsonObjectConst wifi = root["wifi"].as<JsonObjectConst>();
  if (wifi.isNull()) {
    setError(error, errorSize, "config is missing the wifi section");
    return false;
  }
  if (!copyOptionalValue(config.wifiSsid, sizeof(config.wifiSsid), wifi["ssid"]) ||
      !copyOptionalValue(config.wifiPassword, sizeof(config.wifiPassword), wifi["password"])) {
    setError(error, errorSize, "config has an invalid wifi setting");
    return false;
  }

  const JsonObjectConst mqtt = root["mqtt"].as<JsonObjectConst>();
  if (mqtt.isNull() || !copyValue(config.mqttHost, sizeof(config.mqttHost), mqtt["host"]) ||
      !copyOptionalValue(config.mqttUsername, sizeof(config.mqttUsername), mqtt["username"]) ||
      !copyOptionalValue(config.mqttPassword, sizeof(config.mqttPassword), mqtt["password"]) ||
      !copyValue(config.mqttBaseTopic, sizeof(config.mqttBaseTopic), mqtt["base_topic"]) ||
      !readUint16(mqtt, "port", 1, 65535, kDefaultMqttPort, config.mqttPort)) {
    setError(error, errorSize, "config has an invalid mqtt setting");
    return false;
  }
  if (config.mqttHost[0] == '\0' || config.mqttBaseTopic[0] == '\0') {
    setError(error, errorSize, "config is missing a required mqtt setting");
    return false;
  }

  const JsonObjectConst display = root["display"].as<JsonObjectConst>();
  if (!display.isNull()) {
    if (!copyOptionalValue(config.temperatureUnit, sizeof(config.temperatureUnit), display["temperature_unit"]) ||
        !readUint16(display, "expected_poll_interval_seconds", 1, 65535, kDefaultPollIntervalSeconds,
                    config.expectedPollIntervalSeconds) ||
        !readUint16(display, "sleep_after_seconds", 0, 65535, kDefaultSleepAfterSeconds, config.sleepAfterSeconds) ||
        !readBool(display, "show_brightness", true, config.showBrightness) ||
        !readBool(display, "show_wifi", true, config.showWifi)) {
      setError(error, errorSize, "config has an invalid display setting");
      return false;
    }
  }

  const JsonObjectConst logging = root["logging"].as<JsonObjectConst>();
  if (!logging.isNull() && !copyOptionalValue(config.serialLevel, sizeof(config.serialLevel), logging["serial_level"])) {
    setError(error, errorSize, "config has an invalid logging setting");
    return false;
  }

  const JsonObjectConst catalogSection = root["catalog"].as<JsonObjectConst>();
  if (catalogSection.isNull() || !catalogSection["sources"].is<JsonArrayConst>() ||
      !catalogSection["palettes"].is<JsonArrayConst>() || !catalogSection["items"].is<JsonArrayConst>()) {
    setError(error, errorSize, "config catalog section is malformed");
    return false;
  }
  for (JsonObjectConst source : catalogSection["sources"].as<JsonArrayConst>()) {
    if (catalog.sourceCount >= kMaximumTelemetrySources ||
        !parseSource(source, catalog, catalog.sources[catalog.sourceCount])) {
      setError(error, errorSize, "config catalog contains an invalid source");
      return false;
    }
    ++catalog.sourceCount;
  }
  if (catalog.sourceCount == 0) {
    setError(error, errorSize, "config catalog has no sources");
    return false;
  }
  for (JsonObjectConst palette : catalogSection["palettes"].as<JsonArrayConst>()) {
    if (catalog.paletteCount >= kMaximumTelemetryPalettes ||
        !parsePalette(palette, catalog, catalog.palettes[catalog.paletteCount])) {
      setError(error, errorSize, "config catalog contains an invalid palette");
      return false;
    }
    ++catalog.paletteCount;
  }
  if (catalog.paletteCount == 0) {
    setError(error, errorSize, "config catalog has no palettes");
    return false;
  }
  for (JsonObjectConst item : catalogSection["items"].as<JsonArrayConst>()) {
    if (catalog.itemCount >= kMaximumTelemetryDisplays || !parseItem(item, catalog, catalog.items[catalog.itemCount])) {
      setError(error, errorSize, "config catalog contains an invalid item");
      return false;
    }
    ++catalog.itemCount;
  }
  if (catalog.itemCount == 0) {
    setError(error, errorSize, "config catalog has no items");
    return false;
  }
  return true;
}

bool ConfigStore::load(AppConfig &config, DisplayCatalog &catalog, char *error, size_t errorSize) {
  if (!SPIFFS.begin(false)) {
    setError(error, errorSize, "SPIFFS mount failed");
    return false;
  }

  File file = SPIFFS.open(kConfigFilePath, FILE_READ);
  if (!file) {
    setErrorf(error, errorSize, "%s is missing", kConfigFilePath);
    return false;
  }
  if (file.size() == 0 || file.size() > kMaximumConfigBytes) {
    file.close();
    setErrorf(error, errorSize, "%s has an invalid size", kConfigFilePath);
    return false;
  }

  JsonDocument document;
  const DeserializationError jsonError = deserializeJson(document, file);
  file.close();
  if (jsonError) {
    setErrorf(error, errorSize, "%s is malformed JSON", kConfigFilePath);
    return false;
  }
  return validateDocument(document, config, catalog, error, errorSize);
}

void ConfigStore::toJson(const AppConfig &config, const DisplayCatalog &catalog, ArduinoJson::JsonDocument &document) {
  document.clear();
  JsonObject wifi = document["wifi"].to<JsonObject>();
  wifi["ssid"] = config.wifiSsid;
  wifi["password"] = config.wifiPassword;
  JsonObject mqtt = document["mqtt"].to<JsonObject>();
  mqtt["host"] = config.mqttHost;
  mqtt["port"] = config.mqttPort;
  mqtt["username"] = config.mqttUsername;
  mqtt["password"] = config.mqttPassword;
  mqtt["base_topic"] = config.mqttBaseTopic;
  JsonObject display = document["display"].to<JsonObject>();
  display["temperature_unit"] = config.temperatureUnit;
  display["expected_poll_interval_seconds"] = config.expectedPollIntervalSeconds;
  display["sleep_after_seconds"] = config.sleepAfterSeconds;
  display["show_brightness"] = config.showBrightness;
  display["show_wifi"] = config.showWifi;
  JsonObject logging = document["logging"].to<JsonObject>();
  logging["serial_level"] = config.serialLevel;

  JsonObject catalogSection = document["catalog"].to<JsonObject>();
  JsonArray sources = catalogSection["sources"].to<JsonArray>();
  for (size_t index = 0; index < catalog.sourceCount; ++index) {
    JsonObject source = sources.add<JsonObject>();
    source["id"] = catalog.sources[index].id;
    source["topic"] = catalog.sources[index].topic;
  }
  JsonArray palettes = catalogSection["palettes"].to<JsonArray>();
  for (size_t index = 0; index < catalog.paletteCount; ++index) {
    JsonObject palette = palettes.add<JsonObject>();
    palette["id"] = catalog.palettes[index].id;
    char color[8];
    snprintf(color, sizeof(color), "#%06lX", static_cast<unsigned long>(catalog.palettes[index].tickLabelColor));
    palette["tick_label_color"] = color;
    snprintf(color, sizeof(color), "#%06lX", static_cast<unsigned long>(catalog.palettes[index].displayBackground));
    palette["display_background"] = color;
  }
  JsonArray items = catalogSection["items"].to<JsonArray>();
  for (size_t index = 0; index < catalog.itemCount; ++index) {
    const TelemetryDisplayDefinition &definition = catalog.items[index];
    JsonObject item = items.add<JsonObject>();
    item["title"] = definition.title;
    item["carousel_title"] = definition.carouselTitle;
    item["source"] = definition.sourceId;
    item["palette"] = definition.paletteId;
    item["value_key"] = definition.valueKey;
    item["unit"] = definition.unit;
    item["icon"] = definition.icon;
    item["screen"] = definition.screen;
    item["arc_min"] = definition.arcMinimum;
    item["arc_max"] = definition.arcMaximum;
    item["precision"] = definition.precision;
    item["compact"] = definition.compact;
    item["font_size"] = definition.fontSize;
  }
}
