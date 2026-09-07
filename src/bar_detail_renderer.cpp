#include "bar_detail_renderer.h"

#include <Arduino.h>
#include <math.h>

#include "ui.h"
#include "ui_controller.h"

void BarDetailRenderer::ensureObjects() {
	if (bar_) return;
	bar_ = lv_bar_create(ui_Screen2);
	lv_obj_set_size(bar_, 172, 24);
	lv_obj_align(bar_, LV_ALIGN_CENTER, 0, 28);
	lv_obj_set_style_bg_color(bar_, lv_color_hex(0x27424D), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(bar_, lv_color_hex(0x33DCFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
	valueLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(valueLabel_, LV_ALIGN_CENTER, 0, -22);
	lv_obj_set_style_text_font(valueLabel_, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
	rangeLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(rangeLabel_, LV_ALIGN_CENTER, 0, 58);
	lv_obj_set_style_text_font(rangeLabel_, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void BarDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	ensureObjects();
	lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(ui_Label4, item.title);
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_VolumeArc, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	char text[20];
	formatCurrentValue(item, snapshot, hasSnapshot, text, sizeof(text));
	lv_label_set_text(valueLabel_, text);
	lv_bar_set_range(bar_, item.arcMinimum, item.arcMaximum);
	const int value = hasSnapshot && snapshot.values[item.telemetryIndex].available
						? constrain(static_cast<int>(lroundf(snapshot.values[item.telemetryIndex].value)), item.arcMinimum, item.arcMaximum)
						: item.arcMinimum;
	lv_bar_set_value(bar_, value, LV_ANIM_OFF);
	lv_label_set_text_fmt(rangeLabel_, "%d%s                 %d%s", item.arcMinimum, item.unit, item.arcMaximum, item.unit);
	lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(valueLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(rangeLabel_, LV_OBJ_FLAG_HIDDEN);
}

void BarDetailRenderer::hide() {
	if (!bar_) return;
	lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(valueLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(rangeLabel_, LV_OBJ_FLAG_HIDDEN);
}