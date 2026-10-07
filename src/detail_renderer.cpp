#include "detail_renderer.h"

#include <Arduino.h>
#include <math.h>

#include "config_loader.h"
#include "ui_controller.h"

void DetailRenderer::applyBackground(lv_obj_t *screen, const CarouselItem &item,
									const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	const bool available = hasSnapshot && snapshot.values && item.telemetryIndex < snapshot.valueCount &&
		snapshot.values[item.telemetryIndex].available;
	const float value = available ? snapshot.values[item.telemetryIndex].value : 0.0F;
	const uint32_t color = backgroundBandColor(item.backgroundBands, item.backgroundBandCount,
		value, available, item.displayBackground);
	// A dial image can obscure solid colors; band-enabled items use a solid
	// background even during no-data and unmatched-range fallback.
	if (item.backgroundBandCount) lv_obj_set_style_bg_image_src(screen, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(screen, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
}

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