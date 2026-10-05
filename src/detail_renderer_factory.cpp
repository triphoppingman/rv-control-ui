#include "detail_renderer_factory.h"

#include <new>

#include "ui_controller.h"

DetailRendererFactory::~DetailRendererFactory() {
	release();
}

void DetailRendererFactory::release() {
	delete[] chartRenderers_;
	delete[] barRenderers_;
	delete[] thresholdRenderers_;
	delete[] powerFlowRenderers_;
	delete[] itemRenderers_;
	chartRenderers_ = nullptr;
	barRenderers_ = nullptr;
	thresholdRenderers_ = nullptr;
	powerFlowRenderers_ = nullptr;
	itemRenderers_ = nullptr;
	itemCount_ = 0;
}

bool DetailRendererFactory::configure(const DisplayCatalog &catalog) {
	release();
	size_t chartCount = 0;
	size_t barCount = 0;
	size_t thresholdCount = 0;
	size_t powerFlowCount = 0;
	for (size_t index = 0; index < catalog.itemCount; ++index) {
		switch (catalog.items[index].displayMode) {
			case TelemetryDisplayMode::Chart: ++chartCount; break;
			case TelemetryDisplayMode::Bar: ++barCount; break;
			case TelemetryDisplayMode::Threshold: ++thresholdCount; break;
			case TelemetryDisplayMode::PowerFlow: ++powerFlowCount; break;
			default: break;
		}
	}

	itemRenderers_ = new (std::nothrow) DetailRenderer *[catalog.itemCount]();
	if (chartCount) chartRenderers_ = new (std::nothrow) ChartDetailRenderer[chartCount]();
	if (barCount) barRenderers_ = new (std::nothrow) BarDetailRenderer[barCount]();
	if (thresholdCount) thresholdRenderers_ = new (std::nothrow) ThresholdDetailRenderer[thresholdCount]();
	if (powerFlowCount) powerFlowRenderers_ = new (std::nothrow) PowerFlowRenderer[powerFlowCount]();
	if (!itemRenderers_ || (chartCount && !chartRenderers_) || (barCount && !barRenderers_) ||
			(thresholdCount && !thresholdRenderers_) || (powerFlowCount && !powerFlowRenderers_)) {
		release();
		return false;
	}

	size_t chartIndex = 0;
	size_t barIndex = 0;
	size_t thresholdIndex = 0;
	size_t powerFlowIndex = 0;
	for (size_t index = 0; index < catalog.itemCount; ++index) {
		switch (catalog.items[index].displayMode) {
			case TelemetryDisplayMode::Chart:
				chartRenderers_[chartIndex].configure(index);
				itemRenderers_[index] = &chartRenderers_[chartIndex++];
				break;
			case TelemetryDisplayMode::Bar:
				itemRenderers_[index] = &barRenderers_[barIndex++];
				break;
			case TelemetryDisplayMode::Threshold:
				itemRenderers_[index] = &thresholdRenderers_[thresholdIndex++];
				break;
			case TelemetryDisplayMode::PowerFlow:
				itemRenderers_[index] = &powerFlowRenderers_[powerFlowIndex++];
				break;
			case TelemetryDisplayMode::Brightness:
				itemRenderers_[index] = &brightnessRenderer_;
				break;
			case TelemetryDisplayMode::Wifi:
				itemRenderers_[index] = &wifiRenderer_;
				break;
			default:
				itemRenderers_[index] = &dialRenderer_;
				break;
		}
	}
	itemCount_ = catalog.itemCount;
	return true;
}

DetailRenderer &DetailRendererFactory::rendererFor(const CarouselItem &item) {
	if (item.telemetryIndex < itemCount_ && itemRenderers_[item.telemetryIndex]) {
		return *itemRenderers_[item.telemetryIndex];
	}
	return dialRenderer_;
}
