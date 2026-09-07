#pragma once

#include <lvgl.h>
#include <stdint.h>

/**
 * @brief Owns the Elecrow CrowPanel GC9A01 display, LVGL registration, and backlight.
 *
 * This concrete board component keeps LovyanGFX, DMA buffers, and display GPIO
 * details out of the UI controller. It is initialized before SquareLine creates
 * any LVGL screens and is only used from Arduino's loop thread thereafter.
 */
class ElecrowCrowPanelDisplay {
 public:
	/** @brief Return the one board display present in this firmware. */
	static ElecrowCrowPanelDisplay &instance();

	/** @brief Initialize board power, panel DMA, LVGL, PSRAM buffers, and backlight PWM. */
	bool begin();

	/** @brief Return the LVGL display registered for this physical panel. */
	lv_display_t *lvglDisplay() const;

	/** @brief Store and apply the requested visible backlight percentage. */
	void setBacklightPercent(uint8_t percent);

	/** @brief Enable or blank the backlight without losing the selected percentage. */
	void setBacklightEnabled(bool enabled);

 private:
	/** @brief Construct the singleton; use instance() to access the board display. */
	ElecrowCrowPanelDisplay() = default;

	/** @brief Transfer one LVGL-rendered rectangle through the panel DMA engine. */
	void flush(const lv_area_t *area, uint8_t *pixels);

	/** @brief Adapt LVGL's C callback to the board-display instance stored as user data. */
	static void flushCallback(lv_display_t *display, const lv_area_t *area, uint8_t *pixels);

	class PanelDriver;
	PanelDriver *panelDriver_ = nullptr;
	lv_display_t *lvglDisplay_ = nullptr;
	uint8_t *primaryBuffer_ = nullptr;
	uint8_t *secondaryBuffer_ = nullptr;
	uint8_t brightnessPercent_ = 50;
	bool backlightEnabled_ = true;
};