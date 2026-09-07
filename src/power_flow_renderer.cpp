#include "power_flow_renderer.h"

#include <Arduino.h>
#include <string.h>

#include "config_loader.h"
#include "ui.h"
#include "ui_controller.h"

void PowerFlowRenderer::ensureObjects() {
	if (flowLabel_) return;
	sourceLabel_ = lv_label_create(ui_Screen2);
	batteryLabel_ = lv_label_create(ui_Screen2);
	loadLabel_ = lv_label_create(ui_Screen2);
	flowLabel_ = lv_label_create(ui_Screen2);
	lv_obj_align(sourceLabel_, LV_ALIGN_LEFT_MID, 20, -15);
	lv_obj_align(batteryLabel_, LV_ALIGN_CENTER, 0, 28);
	lv_obj_align(loadLabel_, LV_ALIGN_RIGHT_MID, -20, -15);
	lv_obj_align(flowLabel_, LV_ALIGN_CENTER, 0, -45);
	for (lv_obj_t *label : {sourceLabel_, batteryLabel_, loadLabel_, flowLabel_}) {
		lv_obj_set_style_text_font(label, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
	}
}

const TelemetryValue *PowerFlowRenderer::findValue(const char *key, const TelemetrySnapshot &snapshot) {
	const DisplayCatalog &catalog = DisplayCatalog::instance();
	for (size_t index = 0; index < catalog.itemCount; ++index) {
		if (strcmp(catalog.items[index].valueKey, key) == 0) return &snapshot.values[index];
	}
	return nullptr;
}

void PowerFlowRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	ensureObjects();
	lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(ui_Label4, item.title);
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_VolumeArc, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	const TelemetryValue *source = hasSnapshot ? findValue(item.flowSourceKey, snapshot) : nullptr;
	const TelemetryValue *battery = hasSnapshot ? findValue(item.flowBatteryKey, snapshot) : nullptr;
	const TelemetryValue *load = hasSnapshot ? findValue(item.flowLoadKey, snapshot) : nullptr;
	lv_label_set_text_fmt(sourceLabel_, "SOURCE\n%s", source && source->available ? String(source->value, 0).c_str() : "--");
	lv_label_set_text_fmt(batteryLabel_, "BATTERY\n%s", battery && battery->available ? String(battery->value, 0).c_str() : "--");
	lv_label_set_text_fmt(loadLabel_, "LOAD\n%s", load && load->available ? String(load->value, 0).c_str() : "--");
	lv_label_set_text(flowLabel_, "SOURCE  >  BATTERY  >  LOAD");
	for (lv_obj_t *label : {sourceLabel_, batteryLabel_, loadLabel_, flowLabel_}) lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void PowerFlowRenderer::hide() {
	if (!flowLabel_) return;
	for (lv_obj_t *label : {sourceLabel_, batteryLabel_, loadLabel_, flowLabel_}) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
}