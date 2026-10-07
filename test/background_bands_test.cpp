#include <assert.h>
#include <stdio.h>
#include <string>

#include "config_loader.h"

/** @brief A minimal, non-device-specific catalog fixture. */
static const char *fixture = R"({
	"wifi": {},
	"mqtt": {"host": "mqtt.local", "base_topic": "rv"},
	"catalog": {
		"sources": [{"id": "test", "topic": "test"}],
		"palettes": [{"id": "default", "display_background": "#010203"}],
		"items": [{
			"title": "Test", "carousel_title": "Test", "source": "test",
			"palette": "default", "value_key": "value", "unit": "V",
			"icon": "battery", "screen": "electrical"
		}]
	}
})";

/** @brief Run the real boot/API validator and require a diagnostic on every rejection. */
static bool validate(const JsonDocument &document, DisplayCatalog &catalog) {
	AppConfig config = {};
	char error[128] = {};
	const bool accepted = ConfigStore::instance().validateDocument(document, config, catalog, error, sizeof(error));
	assert(accepted || error[0] != '\0');
	return accepted;
}

int main() {
	JsonDocument document;
	assert(!deserializeJson(document, fixture));
	DisplayCatalog catalog = {};
	assert(validate(document, catalog));
	assert(catalog.items[0].backgroundBands == nullptr);
	assert(catalog.items[0].backgroundBandCount == 0);

	JsonDocument bandDocument;
	assert(!deserializeJson(bandDocument, R"([
		{"min": -100, "max": 0, "color": "#123456"},
		{"min": 0, "max": 12.5, "color": "#abcdef"},
		{"min": 20, "max": 30, "color": "#654321"}
	])"));
	document["catalog"]["items"][0]["background_bands"].set(bandDocument.as<JsonArrayConst>());
	assert(validate(document, catalog));
	const auto &item = catalog.items[0];
	assert(item.backgroundBandCount == 3);
	const auto color = [&](float value, bool available = true) {
		return backgroundBandColor(item.backgroundBands, item.backgroundBandCount, value, available, item.displayBackground);
	};
	assert(color(-100) == 0x123456);
	assert(color(-0.01F) == 0x123456);
	assert(color(0) == 0xABCDEF);
	assert(color(12.499F) == 0xABCDEF);
	assert(color(12.5F) == 0x010203);
	assert(color(19) == 0x010203);
	assert(color(20) == 0x654321);
	assert(color(30) == 0x010203);
	assert(color(-101) == 0x010203);
	assert(color(0, false) == 0x010203);
	assert(color(NAN) == 0x010203);
	assert(color(INFINITY) == 0x010203);
	assert(!validBackgroundBand({NAN, 1, 0}, nullptr));
	assert(!validBackgroundBand({0, INFINITY, 0}, nullptr));

	AppConfig config = {};
	char error[128] = {};
	assert(ConfigStore::instance().validateDocument(document, config, catalog, error, sizeof(error)));
	JsonDocument serialized;
	ConfigStore::instance().toJson(config, catalog, serialized);
	std::string wire;
	serializeJson(serialized, wire);
	JsonDocument wireDocument;
	assert(!deserializeJson(wireDocument, wire));
	DisplayCatalog roundTrip = {};
	assert(validate(wireDocument, roundTrip));
	assert(roundTrip.items[0].backgroundBandCount == 3);
	assert(roundTrip.items[0].backgroundBands[1].maximum == 12.5F);
	assert(roundTrip.items[0].backgroundBands[1].color == 0xABCDEF);

	// Each item's bands are independent, including their range ordering.
	document["catalog"]["items"].as<JsonArray>().add(wireDocument["catalog"]["items"][0]);
	document["catalog"]["items"][1]["background_bands"][0]["min"] = -200;
	document["catalog"]["items"][1]["background_bands"][0]["color"] = "#FF0000";
	assert(validate(document, catalog));
	assert(catalog.items[0].backgroundBands != catalog.items[1].backgroundBands);
	assert(catalog.items[1].backgroundBands[0].minimum == -200);
	document["catalog"]["items"].as<JsonArray>().remove(1);

	for (const char *invalid : {
		"null", "{}", "1",
		R"([{"min": 0, "max": 1}])",
		R"([{"min": "0", "max": 1, "color": "#123456"}])",
		R"([{"min": 0, "max": true, "color": "#123456"}])",
		R"([{"min": 0, "max": 1, "color": "#XYZ123"}])",
		R"([{"min": 0, "max": 1, "color": "#FFF"}])",
		R"([{"min": 1, "max": 1, "color": "#123456"}])",
		R"([{"min": 2, "max": 1, "color": "#123456"}])",
		R"([{"min": 0, "max": 1e100, "color": "#123456"}])",
		R"([{"min": 0, "max": 10, "color": "#123456"}, {"min": 9, "max": 20, "color": "#123456"}])",
		R"([{"min": 10, "max": 20, "color": "#123456"}, {"min": 0, "max": 10, "color": "#123456"}])",
		R"([false])"
	}) {
		JsonDocument invalidDocument;
		assert(!deserializeJson(invalidDocument, invalid));
		document["catalog"]["items"][0]["background_bands"].set(invalidDocument.as<JsonVariantConst>());
		assert(!validate(document, catalog));
	}

	document["catalog"]["items"][0]["background_bands"].to<JsonArray>();
	assert(validate(document, catalog));
	assert(catalog.items[0].backgroundBandCount == 0);
	document["catalog"]["items"][0]["display_mode"] = "brightness";
	assert(!validate(document, catalog));
	document["catalog"]["items"][0]["display_mode"] = "wifi";
	assert(!validate(document, catalog));
	document["catalog"]["items"][0].remove("background_bands");
	assert(validate(document, catalog));

	for (const char *mode : {"dial", "chart", "bar", "threshold", "power_flow"}) {
		document["catalog"]["items"][0]["display_mode"] = mode;
		document["catalog"]["items"][0]["flow_source_key"] = "value";
		document["catalog"]["items"][0]["flow_battery_key"] = "value";
		document["catalog"]["items"][0]["flow_load_key"] = "value";
		document["catalog"]["items"][0]["background_bands"].set(bandDocument.as<JsonArrayConst>());
		assert(validate(document, catalog));
	}
	catalog.clear();
	assert(catalog.items == nullptr && catalog.backgroundBands == nullptr && catalog.itemCount == 0);
	puts("Background band validation, boundaries, fallback, modes, ownership and API round-trip passed");
}
