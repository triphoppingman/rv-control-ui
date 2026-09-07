#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders a scalar value against configured low, normal, and high operating bands. */
class ThresholdDetailRenderer final : public DetailRenderer {
 public:
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;
	void hide() override;

 private:
	void ensureObjects();
	lv_obj_t *valueLabel_ = nullptr;
	lv_obj_t *statusLabel_ = nullptr;
	lv_obj_t *bandBar_ = nullptr;
};