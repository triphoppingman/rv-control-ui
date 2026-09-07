#include "chart_detail_renderer.h"

#include <Arduino.h>

#include "telemetry_history.h"
#include "ui.h"
#include "ui_controller.h"

void ChartDetailRenderer::configure(size_t catalogIndex) { catalogIndex_ = catalogIndex; }

void ChartDetailRenderer::ensureObjects() {
	if (chart_) return;
	// Each renderer retains its own objects. UiController shows only the selected
	// renderer, but switching away does not discard this item's chart state.
	chart_ = lv_chart_create(ui_Screen2);
	lv_obj_set_size(chart_, 184, 116);
	lv_obj_align(chart_, LV_ALIGN_CENTER, 0, 14);
	lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
	lv_chart_set_div_line_count(chart_, 4, 3);
	lv_obj_set_style_line_width(chart_, 2, LV_PART_ITEMS);
	lv_obj_set_style_line_color(chart_, lv_color_hex(0x33DCFF), LV_PART_ITEMS);
	lv_obj_set_style_line_color(chart_, lv_color_hex(0x406070), LV_PART_MAIN);
	series_ = lv_chart_add_series(chart_, lv_color_hex(0x33DCFF), LV_CHART_AXIS_PRIMARY_Y);
	currentLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(currentLabel_, LV_ALIGN_TOP_MID, 0, 36);
	lv_obj_set_style_text_font(currentLabel_, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
	minimumLabel_ = lv_label_create(ui_Screen2);
	midpointLabel_ = lv_label_create(ui_Screen2);
	maximumLabel_ = lv_label_create(ui_Screen2);
	timeStartLabel_ = lv_label_create(ui_Screen2);
	timeMiddleLabel_ = lv_label_create(ui_Screen2);
	timeEndLabel_ = lv_label_create(ui_Screen2);
	statusLabel_ = lv_label_create(ui_Screen2);
	for (lv_obj_t *label : {minimumLabel_, midpointLabel_, maximumLabel_, timeStartLabel_, timeMiddleLabel_, timeEndLabel_, statusLabel_}) {
		lv_obj_set_style_text_font(label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_text_color(label, lv_color_hex(0xD7E5EA), LV_PART_MAIN | LV_STATE_DEFAULT);
	}
	lv_obj_align(minimumLabel_, LV_ALIGN_LEFT_MID, 4, 63);
	lv_obj_align(midpointLabel_, LV_ALIGN_LEFT_MID, 4, 14);
	lv_obj_align(maximumLabel_, LV_ALIGN_LEFT_MID, 4, -35);
	lv_obj_align(timeStartLabel_, LV_ALIGN_BOTTOM_LEFT, 28, -22);
	lv_obj_align(timeMiddleLabel_, LV_ALIGN_BOTTOM_MID, 0, -22);
	lv_obj_align(timeEndLabel_, LV_ALIGN_BOTTOM_RIGHT, -28, -22);
	lv_obj_align(statusLabel_, LV_ALIGN_BOTTOM_MID, 0, -40);
}

void ChartDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	ensureObjects();
	lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(ui_Label4, item.title);
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_VolumeArc, LV_OBJ_FLAG_HIDDEN);
	lv_chart_set_range(chart_, LV_CHART_AXIS_PRIMARY_Y, item.arcMinimum, item.arcMaximum);
	// A fixed catalog range makes the slope comparable across visits; missing
	// samples remain gaps instead of being interpolated into a misleading line.
	const size_t count = TelemetryHistory::instance().sampleCount(item.telemetryIndex);
	lv_chart_set_point_count(chart_, count > 1 ? count : 2);
	lv_chart_set_all_value(chart_, series_, LV_CHART_POINT_NONE);
	for (size_t index = 0; index < count; ++index) lv_chart_set_value_by_id(chart_, series_, index,
		TelemetryHistory::instance().chartValue(item.telemetryIndex, index, item.arcMinimum, item.arcMaximum));
	// Use the dial renderer's shared formatting path so current values retain
	// the same precision, temperature-unit, and compact-value behavior.
	char text[20];
	formatCurrentValue(item, snapshot, hasSnapshot, text, sizeof(text));
	lv_label_set_text(currentLabel_, text);
	lv_label_set_text_fmt(minimumLabel_, "%d", item.arcMinimum);
	lv_label_set_text_fmt(midpointLabel_, "%d", item.arcMinimum + (item.arcMaximum - item.arcMinimum) / 2);
	lv_label_set_text_fmt(maximumLabel_, "%d", item.arcMaximum);
	lv_label_set_text(timeStartLabel_, count < 2 ? "--" : "-59 samples");
	lv_label_set_text(timeMiddleLabel_, count < 2 ? "--" : "-30");
	lv_label_set_text(timeEndLabel_, "now");
	lv_label_set_text(statusLabel_, count < 2 ? "Collecting history" : "Current session history");
	lv_obj_remove_flag(chart_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(currentLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(minimumLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(midpointLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(maximumLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(timeStartLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(timeMiddleLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(timeEndLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
}

void ChartDetailRenderer::hide() {
	if (!chart_) return;
	// Keep the LVGL objects allocated for this item and simply hide them while
	// another detail renderer owns the shared generated screen.
	lv_obj_add_flag(chart_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(currentLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(minimumLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(midpointLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(maximumLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(timeStartLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(timeMiddleLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(timeEndLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(statusLabel_, LV_OBJ_FLAG_HIDDEN);
}