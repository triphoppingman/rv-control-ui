#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders one catalog item's volatile telemetry history as a line chart. */
class ChartDetailRenderer final : public DetailRenderer {
 public:
	/** @brief Bind this renderer to its catalog item index for stable LVGL object ownership. */
	void configure(size_t catalogIndex);

	/** @brief Render the selected item's current value and RAM-only history line. */
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;

	/** @brief Hide chart and overlay labels when another renderer becomes active. */
	void hide() override;

 private:
	/** @brief Create the retained chart and labels on first use. */
	void ensureObjects();

	size_t catalogIndex_ = 0;
	lv_obj_t *chart_ = nullptr;
	lv_chart_series_t *series_ = nullptr;
	lv_obj_t *currentLabel_ = nullptr;
	lv_obj_t *minimumLabel_ = nullptr;
	lv_obj_t *midpointLabel_ = nullptr;
	lv_obj_t *maximumLabel_ = nullptr;
	lv_obj_t *timeStartLabel_ = nullptr;
	lv_obj_t *timeMiddleLabel_ = nullptr;
	lv_obj_t *timeEndLabel_ = nullptr;
	lv_obj_t *statusLabel_ = nullptr;
};