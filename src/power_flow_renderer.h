#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders configured source, battery, and load values as a compact power-flow view. */
class PowerFlowRenderer final : public DetailRenderer {
 public:
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;
	void hide() override;

 private:
	void ensureObjects();
	static const TelemetryValue *findValue(const char *key, const TelemetrySnapshot &snapshot);
	lv_obj_t *sourceLabel_ = nullptr;
	lv_obj_t *batteryLabel_ = nullptr;
	lv_obj_t *loadLabel_ = nullptr;
	lv_obj_t *flowLabel_ = nullptr;
};