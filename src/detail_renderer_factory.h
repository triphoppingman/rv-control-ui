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
 * @brief Owns the dynamically sized renderer pool used by UiController detail views.
 *
 * It allocates renderer instances only for configured items of each mode and
 * retains them while UiController shows one at a time on the shared detail screen.
 */
class DetailRendererFactory {
 public:
	/** @brief Allocate per-mode renderer slots for the parsed catalog. */
	bool configure(const DisplayCatalog &catalog);

	/** @brief Release per-item renderer storage when the factory is destroyed. */
	~DetailRendererFactory();

	/** @brief Return the renderer associated with one configured carousel item. */
	DetailRenderer &rendererFor(const CarouselItem &item);

 private:
	DialDetailRenderer dialRenderer_;
	BrightnessDetailRenderer brightnessRenderer_;
	WifiDetailRenderer wifiRenderer_;
	ChartDetailRenderer *chartRenderers_ = nullptr;
	BarDetailRenderer *barRenderers_ = nullptr;
	ThresholdDetailRenderer *thresholdRenderers_ = nullptr;
	PowerFlowRenderer *powerFlowRenderers_ = nullptr;
	DetailRenderer **itemRenderers_ = nullptr;
	size_t itemCount_ = 0;

	/** @brief Release all arrays, including partially allocated setup state. */
	void release();
};