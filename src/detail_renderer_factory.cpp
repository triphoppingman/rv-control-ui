#include "detail_renderer_factory.h"

#include "ui_controller.h"

void DetailRendererFactory::configure(size_t telemetryItemCount) {
	const size_t count = telemetryItemCount < rv_control_ui::constants::kMaximumTelemetryDisplays
										 ? telemetryItemCount
										 : rv_control_ui::constants::kMaximumTelemetryDisplays;
	for (size_t index = 0; index < count; ++index) chartRenderers_[index].configure(index);
}

DetailRenderer &DetailRendererFactory::rendererFor(const CarouselItem &item) {
	if (item.displayMode == TelemetryDisplayMode::Chart) return chartRenderers_[item.telemetryIndex];
	if (item.displayMode == TelemetryDisplayMode::Bar) return barRenderers_[item.telemetryIndex];
	if (item.displayMode == TelemetryDisplayMode::Threshold) return thresholdRenderers_[item.telemetryIndex];
	if (item.displayMode == TelemetryDisplayMode::PowerFlow) return powerFlowRenderers_[item.telemetryIndex];
	return dialRenderer_;
}