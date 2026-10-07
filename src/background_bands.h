#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

/** @brief A context-specific background color for the raw interval [minimum, maximum). */
struct BackgroundBand {
	float minimum;
	float maximum;
	uint32_t color;
};

/** @brief Require finite, ascending, non-overlapping ranges; gaps and adjacent endpoints are allowed. */
inline bool validBackgroundBand(const BackgroundBand &band, const BackgroundBand *previous) {
	return isfinite(band.minimum) && isfinite(band.maximum) && band.minimum < band.maximum &&
		(!previous || previous->maximum <= band.minimum);
}

/** @brief Resolve a raw telemetry value without rounding, clamping, or assigning meaning to colors. */
inline uint32_t backgroundBandColor(const BackgroundBand *bands, size_t count, float value,
								   bool available, uint32_t fallback) {
	if (!available || !isfinite(value)) return fallback;
	for (size_t index = 0; index < count; ++index) {
		if (value >= bands[index].minimum && value < bands[index].maximum) return bands[index].color;
	}
	return fallback;
}
