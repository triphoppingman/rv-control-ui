#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders and adjusts the device-local display brightness control. */
class BrightnessDetailRenderer final : public DetailRenderer {
 public:
	/** @brief Render the generated brightness screen and synchronize its current value. */
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;

	/** @brief Apply one rotary adjustment while the brightness detail is active. */
	bool handleRotation(bool clockwise) override;

	/** @brief Hide no retained objects because the generated brightness screen owns them. */
	void hide() override;

 private:
	/** @brief Forward the generated arc change to the UI controller's backlight owner. */
	static void brightnessChangedCallback(lv_event_t *event);
};