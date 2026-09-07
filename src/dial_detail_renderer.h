#pragma once

#include "detail_renderer.h"

/** @brief Renders the existing SquareLine electrical and temperature dial detail views. */
class DialDetailRenderer final : public DetailRenderer {
 public:
	/** @brief Render the selected telemetry value on its configured generated dial screen. */
	void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) override;

	/** @brief Restore generated dial controls after a chart renderer was active. */
	void hide() override;
};