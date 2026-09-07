#include "dial_detail_renderer.h"

#include <Arduino.h>
#include <math.h>

#include "config_loader.h"
#include "ui.h"
#include "ui_controller.h"

void DialDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	// Preserve the existing generated dial surfaces for catalog entries that do
	// not explicitly request the new chart display mode.
	char text[16];
	formatCurrentValue(item, snapshot, hasSnapshot, text, sizeof(text));
	const int value = hasSnapshot && item.telemetryIndex != rv_control_ui::constants::kNoTelemetryIndex && snapshot.values[item.telemetryIndex].available
						? constrain(static_cast<int>(lroundf(snapshot.values[item.telemetryIndex].value)), item.arcMinimum, item.arcMaximum)
						: item.arcMinimum;
	// The generated export provides separate electrical and temperature screens;
	// both remain read-only views of the copied MQTT snapshot.
	if (item.usesTemperatureScreen) {
		lv_obj_set_style_bg_image_src(ui_Screen3, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_color(ui_Screen3, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_label_set_text(ui_Label8, item.title);
		lv_label_set_text(ui_TempNum, text);
		lv_arc_set_range(ui_TempArc, item.arcMinimum, item.arcMaximum);
		lv_arc_set_value(ui_TempArc, value);
		lv_obj_remove_flag(ui_TempArc, LV_OBJ_FLAG_CLICKABLE);
	} else {
		lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_remove_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(ui_Label4, item.title);
		lv_label_set_text(ui_VolNum, text);
		lv_arc_set_range(ui_VolumeArc, item.arcMinimum, item.arcMaximum);
		lv_arc_set_value(ui_VolumeArc, value);
		lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	}
}

void DialDetailRenderer::hide() {
	lv_obj_remove_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
}