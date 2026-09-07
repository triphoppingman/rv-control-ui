#include "detail_renderer.h"

#include <Arduino.h>
#include <math.h>

#include "config_loader.h"
#include "ui_controller.h"

void DetailRenderer::formatCurrentValue(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot,
																	 char *text, size_t textSize) {
	if (!hasSnapshot || item.telemetryIndex == rv_control_ui::constants::kNoTelemetryIndex ||
		!snapshot.values[item.telemetryIndex].available) {
		strlcpy(text, "--", textSize);
		return;
	}
	const char *unit = item.usesTemperatureScreen ? AppConfig::instance().temperatureUnit : item.unit;
	char compactUnit[12] = {};
	float value = snapshot.values[item.telemetryIndex].value;
	if (item.compact && fabsf(value) >= 1000.0F) {
		value /= 1000.0F;
		snprintf(compactUnit, sizeof(compactUnit), "k%s", unit);
		unit = compactUnit;
	}
	snprintf(text, textSize, "%.*f%s", item.precision, value, unit);
}