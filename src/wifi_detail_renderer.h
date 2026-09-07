#pragma once

#include <lvgl.h>

#include "detail_renderer.h"

/** @brief Renders the device-local Wi-Fi connection or setup-hotspot status. */
class WifiDetailRenderer final : public DetailRenderer {
 public:
	/** @brief Render the Wi-Fi detail screen and its current network state. */
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;

	/** @brief Refresh the connection state once per second while this renderer is active. */
	void update() override;

	/** @brief Hide retained Wi-Fi status labels before another renderer takes the screen. */
	void hide() override;

 private:
	/** @brief Create status labels for the currently generated electrical detail screen. */
	void ensureObjects();

	/** @brief Copy the current Wi-Fi state into the rendered labels and signal arc. */
	void refresh();

	lv_obj_t *screen_ = nullptr;
	lv_obj_t *signalLabel_ = nullptr;
	lv_obj_t *infoLabel_ = nullptr;
	uint32_t lastRefreshMilliseconds_ = 0;
};