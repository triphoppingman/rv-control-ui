#pragma once

#include "network_controller.h"

struct CarouselItem;

/** @brief Common loop-thread contract for one telemetry detail presentation. */
class DetailRenderer {
 public:
	virtual ~DetailRenderer() = default;

	/** @brief Render the supplied item using the current copied telemetry snapshot. */
	virtual void render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) = 0;

	/** @brief Hide renderer-owned LVGL objects before another detail renderer is shown. */
	virtual void hide() = 0;

 protected:
	/** @brief Format one current telemetry value consistently across every detail renderer. */
	static void formatCurrentValue(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot,
																	 char *text, size_t textSize);
};