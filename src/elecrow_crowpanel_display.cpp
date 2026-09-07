#define LGFX_USE_V1

#include "elecrow_crowpanel_display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

#include "constants.h"

namespace {

using namespace rv_control_ui::constants;

/** @brief Configures the GC9A01 panel and SPI2 bus for the Elecrow board. */
class Gc9a01PanelDriver : public lgfx::LGFX_Device {
 public:
	/** @brief Record the known-good SPI2 and GC9A01 settings without starting hardware. */
	Gc9a01PanelDriver() {
		auto busConfig = bus_.config();
		busConfig.spi_host = SPI2_HOST;
		busConfig.spi_mode = 0;
		busConfig.freq_write = 80000000;
		busConfig.freq_read = 20000000;
		busConfig.spi_3wire = true;
		busConfig.use_lock = true;
		busConfig.dma_channel = SPI_DMA_CH_AUTO;
		busConfig.pin_sclk = 10;
		busConfig.pin_mosi = 11;
		busConfig.pin_miso = -1;
		busConfig.pin_dc = 3;
		bus_.config(busConfig);
		panel_.setBus(&bus_);

		auto panelConfig = panel_.config();
		panelConfig.pin_cs = 9;
		panelConfig.pin_rst = 14;
		panelConfig.pin_busy = -1;
		panelConfig.memory_width = kScreenWidth;
		panelConfig.memory_height = kScreenHeight;
		panelConfig.panel_width = kScreenWidth;
		panelConfig.panel_height = kScreenHeight;
		panelConfig.offset_x = 0;
		panelConfig.offset_y = 0;
		panelConfig.offset_rotation = 0;
		panelConfig.dummy_read_pixel = 8;
		panelConfig.dummy_read_bits = 1;
		panelConfig.readable = false;
		panelConfig.invert = true;
		panelConfig.rgb_order = false;
		panelConfig.dlen_16bit = false;
		panelConfig.bus_shared = false;
		panel_.config(panelConfig);
		setPanel(&panel_);
	}

 private:
	lgfx::Panel_GC9A01 panel_;
	lgfx::Bus_SPI bus_;
};

}  // namespace

class ElecrowCrowPanelDisplay::PanelDriver : public Gc9a01PanelDriver {};

ElecrowCrowPanelDisplay &ElecrowCrowPanelDisplay::instance() {
	static ElecrowCrowPanelDisplay display;
	return display;
}

bool ElecrowCrowPanelDisplay::begin() {
	pinMode(kPowerLightPin, OUTPUT);
	digitalWrite(kPowerLightPin, LOW);
	pinMode(1, OUTPUT);
	pinMode(2, OUTPUT);
	digitalWrite(1, HIGH);
	digitalWrite(2, HIGH);

	panelDriver_ = new PanelDriver();
	panelDriver_->init();
	panelDriver_->initDMA();
	panelDriver_->startWrite();
	panelDriver_->fillScreen(TFT_BLACK);

	lv_init();
	lv_tick_set_cb(millis);
	const size_t bufferSize = kScreenWidth * kScreenHeight * sizeof(uint16_t);
	primaryBuffer_ = static_cast<uint8_t *>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM));
	secondaryBuffer_ = static_cast<uint8_t *>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM));
	if (!primaryBuffer_ || !secondaryBuffer_) {
		Serial.println("[ERROR] LVGL buffer allocation failed");
		return false;
	}

	lvglDisplay_ = lv_display_create(kScreenWidth, kScreenHeight);
	lv_display_set_color_format(lvglDisplay_, LV_COLOR_FORMAT_RGB565);
	lv_display_set_user_data(lvglDisplay_, this);
	lv_display_set_flush_cb(lvglDisplay_, flushCallback);
	lv_display_set_buffers(lvglDisplay_, primaryBuffer_, secondaryBuffer_, bufferSize, LV_DISPLAY_RENDER_MODE_FULL);
	ledcAttach(kBacklightPin, 5000, 8);
	setBacklightPercent(brightnessPercent_);
	return true;
}

lv_display_t *ElecrowCrowPanelDisplay::lvglDisplay() const { return lvglDisplay_; }

void ElecrowCrowPanelDisplay::setBacklightPercent(uint8_t percent) {
	brightnessPercent_ = constrain(percent, 0, 100);
	if (backlightEnabled_) ledcWrite(kBacklightPin, (brightnessPercent_ * 255U) / 100U);
}

void ElecrowCrowPanelDisplay::setBacklightEnabled(bool enabled) {
	backlightEnabled_ = enabled;
	ledcWrite(kBacklightPin, enabled ? (brightnessPercent_ * 255U) / 100U : 0);
}

void ElecrowCrowPanelDisplay::flush(const lv_area_t *area, uint8_t *pixels) {
	if (panelDriver_->getStartCount() > 0) panelDriver_->endWrite();
	panelDriver_->pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
									 reinterpret_cast<lgfx::rgb565_t *>(pixels));
	panelDriver_->waitDMA();
}

void ElecrowCrowPanelDisplay::flushCallback(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
	auto *boardDisplay = static_cast<ElecrowCrowPanelDisplay *>(lv_display_get_user_data(display));
	boardDisplay->flush(area, pixels);
	lv_display_flush_ready(display);
}