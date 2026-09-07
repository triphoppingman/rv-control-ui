#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders one scalar telemetry value as a fixed-range fill bar. */
class BarDetailRenderer final : public DetailRenderer {
 public:
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;
	void hide() override;

 private:
	void ensureObjects();
	lv_obj_t *bar_ = nullptr;
	lv_obj_t *valueLabel_ = nullptr;
	lv_obj_t *rangeLabel_ = nullptr;
};