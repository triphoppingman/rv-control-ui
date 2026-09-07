#pragma once

#include <lvgl.h>
#include <widgets/chart/lv_chart.h>
#include <stdint.h>

#include "network_controller.h"

/** @brief Fixed-capacity volatile history retained only for the current firmware session. */
class TelemetryHistory {
 public:
	/** @brief Return the one RAM-only telemetry history store. */
	static TelemetryHistory &instance();

	/** @brief Append one accepted MQTT snapshot for every configured telemetry item. */
	void append(const TelemetrySnapshot &snapshot);

	/** @brief Return the ordered sample count retained for one telemetry item. */
	size_t sampleCount(size_t telemetryIndex) const;

	/** @brief Return one oldest-to-newest sample value, or INT32_MAX when unavailable. */
	int32_t chartValue(size_t telemetryIndex, size_t orderedIndex, int minimum, int maximum) const;

 private:
	/** @brief Construct the singleton; all samples are fixed-size in RAM. */
	TelemetryHistory() = default;

	static constexpr size_t kHistorySamples = 60;
	TelemetryValue samples_[rv_control_ui::constants::kMaximumTelemetryDisplays][kHistorySamples] = {};
	size_t count_ = 0;
	size_t nextIndex_ = 0;
};