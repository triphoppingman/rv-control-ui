#include "display_catalog.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <string.h>

namespace {

// The catalog describes a small carousel and must never consume arbitrary SPIFFS memory.
constexpr size_t kMaximumCatalogBytes = 8192;

/** @brief Store a short catalog diagnostic in the caller-provided serial-safe buffer. */
void setError(char *error, size_t errorSize, const char *message) {
	if (errorSize > 0) strlcpy(error, message, errorSize);
}

/** @brief Copy a required JSON string only when it fits the destination field exactly. */
bool copyString(char *destination, size_t destinationSize, JsonVariantConst value) {
	if (!value.is<const char *>()) return false;
	const char *text = value.as<const char *>();
	if (strlen(text) >= destinationSize) return false;
	strlcpy(destination, text, destinationSize);
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
bool parseSource(JsonObjectConst source, DisplayCatalog &catalog, TelemetrySourceDefinition &definition) {
	memset(&definition, 0, sizeof(definition));
	if (!copyString(definition.id, sizeof(definition.id), source["id"]) ||
			!copyString(definition.topic, sizeof(definition.topic), source["topic"]) || !isValidIdentifier(definition.id) ||
			!isValidTopicSuffix(definition.topic)) return false;
	for (size_t index = 0; index < catalog.sourceCount; ++index) {
		if (strcmp(catalog.sources[index].id, definition.id) == 0 || strcmp(catalog.sources[index].topic, definition.topic) == 0) return false;
	}
	return true;
}

/** @brief Parse one named visual palette and reject duplicate or malformed definitions. */
bool parsePalette(JsonObjectConst source, DisplayCatalog &catalog, TelemetryPaletteDefinition &definition) {
	memset(&definition, 0, sizeof(definition));
	if (!copyString(definition.id, sizeof(definition.id), source["id"]) || !isValidIdentifier(definition.id) ||
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
	if (!copyString(item.title, sizeof(item.title), source["title"]) ||
			!copyString(item.carouselTitle, sizeof(item.carouselTitle), source["carousel_title"]) ||
			!copyString(item.sourceId, sizeof(item.sourceId), source["source"]) ||
			!copyString(item.paletteId, sizeof(item.paletteId), source["palette"]) ||
			!copyString(item.valueKey, sizeof(item.valueKey), source["value_key"]) ||
			!copyString(item.unit, sizeof(item.unit), source["unit"]) ||
			!copyString(item.icon, sizeof(item.icon), source["icon"]) ||
			!copyString(item.screen, sizeof(item.screen), source["screen"])) return false;
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

}  // namespace

bool loadDisplayCatalog(DisplayCatalog &catalog, char *error, size_t errorSize) {
	memset(&catalog, 0, sizeof(catalog));
	File file = SPIFFS.open("/display-catalog.json", FILE_READ);
	if (!file) {
		setError(error, errorSize, "/display-catalog.json is missing");
		return false;
	}
	if (file.size() == 0 || file.size() > kMaximumCatalogBytes) {
		file.close();
		setError(error, errorSize, "/display-catalog.json has an invalid size");
		return false;
	}
	JsonDocument document;
	const DeserializationError jsonError = deserializeJson(document, file);
	file.close();
	if (jsonError || !document["sources"].is<JsonArrayConst>() || !document["palettes"].is<JsonArrayConst>() ||
			!document["items"].is<JsonArrayConst>()) {
		setError(error, errorSize, "/display-catalog.json is malformed");
		return false;
	}
	for (JsonObjectConst source : document["sources"].as<JsonArrayConst>()) {
		if (catalog.sourceCount >= kMaximumTelemetrySources || !parseSource(source, catalog, catalog.sources[catalog.sourceCount])) {
			setError(error, errorSize, "/display-catalog.json contains an invalid source");
			return false;
		}
		++catalog.sourceCount;
	}
	if (catalog.sourceCount == 0) {
		setError(error, errorSize, "/display-catalog.json has no sources");
		return false;
	}
	for (JsonObjectConst palette : document["palettes"].as<JsonArrayConst>()) {
		if (catalog.paletteCount >= kMaximumTelemetryPalettes || !parsePalette(palette, catalog, catalog.palettes[catalog.paletteCount])) {
			setError(error, errorSize, "/display-catalog.json contains an invalid palette");
			return false;
		}
		++catalog.paletteCount;
	}
	if (catalog.paletteCount == 0) {
		setError(error, errorSize, "/display-catalog.json has no palettes");
		return false;
	}
	for (JsonObjectConst source : document["items"].as<JsonArrayConst>()) {
		if (catalog.itemCount >= kMaximumTelemetryDisplays || !parseItem(source, catalog, catalog.items[catalog.itemCount])) {
			setError(error, errorSize, "/display-catalog.json contains an invalid item");
			return false;
		}
		++catalog.itemCount;
	}
	if (catalog.itemCount == 0) {
		setError(error, errorSize, "/display-catalog.json has no items");
		return false;
	}
	return true;
}