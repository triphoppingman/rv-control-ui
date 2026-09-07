#pragma once

#include "bar_detail_renderer.h"
#include "brightness_detail_renderer.h"
#include "chart_detail_renderer.h"
#include "dial_detail_renderer.h"
#include "power_flow_renderer.h"
#include "threshold_detail_renderer.h"
#include "wifi_detail_renderer.h"

struct CarouselItem;

/**
 * @brief Owns the preallocated renderer pool used by UiController detail views.
 *
 * The factory creates no objects dynamically. It retains a renderer instance
 * for every telemetry item and returns the concrete renderer selected by the
 * validated display mode, while UiController remains responsible for showing
 * only one renderer at a time on the shared generated detail screen.
 */
class DetailRendererFactory {
 public:
	/** @brief Configure per-item renderer slots after the telemetry catalog is loaded. */
	void configure(size_t telemetryItemCount);

	/** @brief Return the renderer associated with one configured carousel item. */
	DetailRenderer &rendererFor(const CarouselItem &item);

 private:
	DialDetailRenderer dialRenderer_;
	BrightnessDetailRenderer brightnessRenderer_;
	WifiDetailRenderer wifiRenderer_;
	ChartDetailRenderer chartRenderers_[rv_control_ui::constants::kMaximumTelemetryDisplays];
	BarDetailRenderer barRenderers_[rv_control_ui::constants::kMaximumTelemetryDisplays];
	ThresholdDetailRenderer thresholdRenderers_[rv_control_ui::constants::kMaximumTelemetryDisplays];
	PowerFlowRenderer powerFlowRenderers_[rv_control_ui::constants::kMaximumTelemetryDisplays];
};