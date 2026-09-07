#include "threshold_detail_renderer.h"

#include <Arduino.h>
#include <math.h>

#include "ui.h"
#include "ui_controller.h"

void ThresholdDetailRenderer::ensureObjects() {
	if (bandBar_) return;
	bandBar_ = lv_bar_create(ui_Screen2);
	lv_obj_set_size(bandBar_, 172, 24);
	lv_obj_align(bandBar_, LV_ALIGN_CENTER, 0, 30);
	valueLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(valueLabel_, LV_ALIGN_CENTER, 0, -20);
	lv_obj_set_style_text_font(valueLabel_, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
	statusLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(statusLabel_, LV_ALIGN_CENTER, 0, 58);
	lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ThresholdDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
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
	lv_bar_set_range(bandBar_, item.arcMinimum, item.arcMaximum);
	const bool available = hasSnapshot && snapshot.values[item.telemetryIndex].available;
	const float rawValue = available ? snapshot.values[item.telemetryIndex].value : item.arcMinimum;
	lv_bar_set_value(bandBar_, constrain(static_cast<int>(lroundf(rawValue)), item.arcMinimum, item.arcMaximum), LV_ANIM_OFF);
	const uint32_t color = !available ? 0x808080 : rawValue < item.thresholdLow || rawValue > item.thresholdHigh ? 0xFF6B35 : 0x39D98A;
	lv_obj_set_style_bg_color(bandBar_, lv_color_hex(color), LV_PART_INDICATOR | LV_STATE_DEFAULT);
	lv_label_set_text(statusLabel_, !available ? "No data" : rawValue < item.thresholdLow ? "Below normal range" : rawValue > item.thresholdHigh ? "Above normal range" : "Normal range");
	lv_obj_remove_flag(bandBar_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(valueLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
}

void ThresholdDetailRenderer::hide() {
	if (!bandBar_) return;
	lv_obj_add_flag(bandBar_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(valueLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
}