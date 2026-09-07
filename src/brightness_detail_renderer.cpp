#include "brightness_detail_renderer.h"

#include <Arduino.h>

#include "ui.h"
#include "ui_controller.h"

void BrightnessDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	(void)snapshot;
	(void)hasSnapshot;
	lv_obj_set_style_bg_color(ui_Screen4, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(ui_Label13, item.title);
	lv_arc_set_range(ui_lightArc, item.arcMinimum, item.arcMaximum);
	lv_arc_set_value(ui_lightArc, UiController::instance().brightnessPercent());
	lv_obj_remove_event_cb(ui_lightArc, brightnessChangedCallback);
	lv_obj_add_event_cb(ui_lightArc, brightnessChangedCallback, LV_EVENT_VALUE_CHANGED, nullptr);
	lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);
}

bool BrightnessDetailRenderer::handleRotation(bool clockwise) {
	const int brightness = constrain(lv_arc_get_value(ui_lightArc) + (clockwise ? 5 : -5), 0, 100);
	lv_arc_set_value(ui_lightArc, brightness);
	lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);
	return true;
}

void BrightnessDetailRenderer::hide() {}

void BrightnessDetailRenderer::brightnessChangedCallback(lv_event_t *event) {
	UiController::instance().handleBrightnessChanged(event);
}