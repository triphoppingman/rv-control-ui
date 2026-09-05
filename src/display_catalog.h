#pragma once

#include <stddef.h>
#include <stdint.h>

/** @brief Maximum MQTT-backed display definitions accepted from the SPIFFS catalog. */
constexpr size_t kMaximumTelemetryDisplays = 16;
/** @brief Maximum MQTT source topics accepted from the SPIFFS catalog. */
constexpr size_t kMaximumTelemetrySources = 8;
/** @brief Maximum visual palettes accepted from the SPIFFS catalog. */
constexpr size_t kMaximumTelemetryPalettes = 8;

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
 * @brief One read-only MQTT display definition loaded from /display-catalog.json.
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
	TelemetrySourceDefinition sources[kMaximumTelemetrySources];
	size_t sourceCount;
	TelemetryPaletteDefinition palettes[kMaximumTelemetryPalettes];
	size_t paletteCount;
	TelemetryDisplayDefinition items[kMaximumTelemetryDisplays];
	size_t itemCount;
};

/**
 * @brief Read and validate the telemetry display catalog from SPIFFS.
 *
 * Sources define the allowed topic suffixes. Brightness and Wi-Fi Info remain
 * local firmware entries and are excluded from this file.
 */
bool loadDisplayCatalog(DisplayCatalog &catalog, char *error, size_t errorSize);