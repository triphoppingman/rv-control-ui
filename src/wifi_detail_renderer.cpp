#include "wifi_detail_renderer.h"

#include <Arduino.h>
#include <WiFi.h>

#include "ui.h"
#include "ui_controller.h"

void WifiDetailRenderer::render(const CarouselItem &item, const TelemetrySnapshot &snapshot, bool hasSnapshot) {
	(void)snapshot;
	(void)hasSnapshot;
	ensureObjects();
	lv_obj_set_style_bg_image_src(ui_Screen2, nullptr, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(ui_Screen2, lv_color_hex(item.displayBackground), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_label_set_text(ui_Label4, item.title);
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_HIDDEN);
	refresh();
}

void WifiDetailRenderer::update() {
	if (millis() - lastRefreshMilliseconds_ < 1000U) return;
	refresh();
}

void WifiDetailRenderer::hide() {
	if (signalLabel_) lv_obj_add_flag(signalLabel_, LV_OBJ_FLAG_HIDDEN);
	if (infoLabel_) lv_obj_add_flag(infoLabel_, LV_OBJ_FLAG_HIDDEN);
}

void WifiDetailRenderer::ensureObjects() {
	if (screen_ == ui_Screen2) return;
	screen_ = ui_Screen2;
	signalLabel_ = lv_label_create(screen_);
	lv_obj_set_size(signalLabel_, 150, 56);
	lv_obj_align(signalLabel_, LV_ALIGN_CENTER, 4, 3);
	lv_obj_set_style_text_align(signalLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(signalLabel_, lv_color_hex(0x33DCFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(signalLabel_, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
	infoLabel_ = lv_label_create(screen_);
	lv_obj_set_width(infoLabel_, 180);
	lv_obj_align(infoLabel_, LV_ALIGN_CENTER, 0, 52);
	lv_obj_set_style_text_align(infoLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(infoLabel_, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(infoLabel_, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void WifiDetailRenderer::refresh() {
	lastRefreshMilliseconds_ = millis();
	const bool hotspot = (WiFi.getMode() & WIFI_MODE_AP) != 0 && WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
	const bool connected = !hotspot && WiFi.status() == WL_CONNECTED;
	char signalText[16] = {};
	int signalPercent = 0;
	if (hotspot) {
		strlcpy(signalText, "Setup", sizeof(signalText));
		signalPercent = 100;
	} else if (connected) {
		const int rssi = WiFi.RSSI();
		signalPercent = constrain(((rssi + 100) * 100) / 60, 0, 100);
		snprintf(signalText, sizeof(signalText), "%ddBm", rssi);
	} else {
		strlcpy(signalText, "Offline", sizeof(signalText));
	}
	lv_label_set_text(signalLabel_, signalText);
	lv_arc_set_range(ui_VolumeArc, 0, 100);
	lv_arc_set_value(ui_VolumeArc, signalPercent);
	lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_CLICKABLE);
	if (hotspot) {
		lv_label_set_text_fmt(infoLabel_, "Hotspot: %s\nIP: %s", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	} else if (connected) {
		const String ipAddress = WiFi.localIP().toString();
		lv_label_set_text_fmt(infoLabel_, "SSID: %s\nIP: %s", WiFi.SSID().c_str(), ipAddress.c_str());
	} else {
		lv_label_set_text(infoLabel_, "SSID: --\nIP: --");
	}
	lv_obj_remove_flag(signalLabel_, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(infoLabel_, LV_OBJ_FLAG_HIDDEN);
}