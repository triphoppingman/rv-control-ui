#include "telemetry_history.h"

#include <Arduino.h>
#include <new>

TelemetryHistory &TelemetryHistory::instance() {
	static TelemetryHistory history;
	return history;
}

bool TelemetryHistory::configure(size_t telemetryItemCount) {
	delete[] samples_;
	samples_ = new (std::nothrow) TelemetryValue[telemetryItemCount * kHistorySamples]();
	if (!samples_) {
		telemetryItemCount_ = 0;
		return false;
	}
	telemetryItemCount_ = telemetryItemCount;
	count_ = 0;
	nextIndex_ = 0;
	return true;
}

void TelemetryHistory::append(const TelemetrySnapshot &snapshot) {
	// The UI loop records only accepted snapshots. This is volatile RAM history,
	// so periodic telemetry never writes SPIFFS or any other flash-backed store.
	if (!samples_ || !snapshot.values || snapshot.valueCount != telemetryItemCount_) return;
	for (size_t index = 0; index < telemetryItemCount_; ++index) {
		samples_[index * kHistorySamples + nextIndex_] = snapshot.values[index];
	}
	nextIndex_ = (nextIndex_ + 1) % kHistorySamples;
	if (count_ < kHistorySamples) ++count_;
}

size_t TelemetryHistory::sampleCount(size_t telemetryIndex) const {
	if (telemetryIndex >= telemetryItemCount_) return 0;
	return count_;
}

int32_t TelemetryHistory::chartValue(size_t telemetryIndex, size_t orderedIndex, int minimum, int maximum) const {
	if (telemetryIndex >= telemetryItemCount_ || orderedIndex >= count_ || !samples_) return INT32_MAX;
	// nextIndex_ points at the next overwrite slot; derive the oldest retained
	// sample so LVGL receives values in chronological left-to-right order.
	const size_t oldest = (nextIndex_ + kHistorySamples - count_) % kHistorySamples;
	const TelemetryValue sample = samples_[telemetryIndex * kHistorySamples + (oldest + orderedIndex) % kHistorySamples];
	if (!sample.available) return INT32_MAX;
	return constrain(static_cast<int32_t>(lroundf(sample.value)), minimum, maximum);
}