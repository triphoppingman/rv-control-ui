#include "telemetry_history.h"

#include <Arduino.h>

TelemetryHistory &TelemetryHistory::instance() {
	static TelemetryHistory history;
	return history;
}

void TelemetryHistory::append(const TelemetrySnapshot &snapshot) {
	// The UI loop records only accepted snapshots. This is volatile RAM history,
	// so periodic telemetry never writes SPIFFS or any other flash-backed store.
	for (size_t index = 0; index < rv_control_ui::constants::kMaximumTelemetryDisplays; ++index) {
		samples_[index][nextIndex_] = snapshot.values[index];
	}
	nextIndex_ = (nextIndex_ + 1) % kHistorySamples;
	if (count_ < kHistorySamples) ++count_;
}

size_t TelemetryHistory::sampleCount(size_t telemetryIndex) const {
	if (telemetryIndex >= rv_control_ui::constants::kMaximumTelemetryDisplays) return 0;
	return count_;
}

int32_t TelemetryHistory::chartValue(size_t telemetryIndex, size_t orderedIndex, int minimum, int maximum) const {
	if (telemetryIndex >= rv_control_ui::constants::kMaximumTelemetryDisplays || orderedIndex >= count_) return INT32_MAX;
	// nextIndex_ points at the next overwrite slot; derive the oldest retained
	// sample so LVGL receives values in chronological left-to-right order.
	const size_t oldest = (nextIndex_ + kHistorySamples - count_) % kHistorySamples;
	const TelemetryValue sample = samples_[telemetryIndex][(oldest + orderedIndex) % kHistorySamples];
	if (!sample.available) return INT32_MAX;
	return constrain(static_cast<int32_t>(lroundf(sample.value)), minimum, maximum);
}