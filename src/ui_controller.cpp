#include "ui_controller.h"

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include "constants.h"
#include "elecrow_crowpanel_input.h"
#include "telemetry_history.h"
#include "ui.h"

using namespace rv_control_ui::constants;

UiController &UiController::instance() {
	static UiController controller;
	return controller;
}

void UiController::begin() { display_ = &ElecrowCrowPanelDisplay::instance(); }

void UiController::configure() {
	sleepAfterSeconds_ = AppConfig::instance().sleepAfterSeconds;
	lastUserActivityMilliseconds_ = millis();
}

void UiController::initializeCarousel() {
	const AppConfig &config = AppConfig::instance();
	const DisplayCatalog &catalog = DisplayCatalog::instance();
	carouselItemCount_ = 0;
	selectedItemIndex_ = 0;
	for (size_t index = 0; index < catalog.itemCount; ++index) {
		const TelemetryDisplayDefinition &definition = catalog.items[index];
		CarouselIcon icon = CarouselIcon::Battery;
		if (strcmp(definition.icon, "solar") == 0) icon = CarouselIcon::Solar;
		else if (strcmp(definition.icon, "load") == 0) icon = CarouselIcon::Load;
		else if (strcmp(definition.icon, "temperature") == 0) icon = CarouselIcon::Temperature;
		carouselItems_[carouselItemCount_++] = {definition.title, definition.carouselTitle, definition.unit, index, icon,
			strcmp(definition.screen, "temperature") == 0, false, false, definition.arcMinimum, definition.arcMaximum,
			definition.tickLabelColor, definition.displayBackground, definition.precision, definition.compact, definition.fontSize,
			definition.displayMode, definition.thresholdLow, definition.thresholdHigh, {}, {}, {}};
		strlcpy(carouselItems_[carouselItemCount_ - 1].flowSourceKey, definition.flowSourceKey, sizeof(definition.flowSourceKey));
		strlcpy(carouselItems_[carouselItemCount_ - 1].flowBatteryKey, definition.flowBatteryKey, sizeof(definition.flowBatteryKey));
		strlcpy(carouselItems_[carouselItemCount_ - 1].flowLoadKey, definition.flowLoadKey, sizeof(definition.flowLoadKey));
	}
	rendererFactory_.configure(catalog.itemCount);
	if (config.showBrightness) {
		carouselItems_[carouselItemCount_++] = {"Brightness", "Brightness", "%", kNoTelemetryIndex, CarouselIcon::Brightness,
			false, true, false, 0, 100, 0xFFFFFF, 0x000000, 0, false, 40, TelemetryDisplayMode::Dial, 0, 100, {}, {}, {}};
	}
	if (config.showWifi) {
		carouselItems_[carouselItemCount_++] = {"WiFi info", "WiFi\nInfo", "", kNoTelemetryIndex, CarouselIcon::WiFi,
			false, false, true, 0, 100, 0xFFFFFF, 0x000000, 0, false, 20, TelemetryDisplayMode::Dial, 0, 100, {}, {}, {}};
	}
}

void UiController::registerGeneratedCallbacks() {
	lv_obj_add_event_cb(ui_lightArc, brightnessChangedCallback, LV_EVENT_VALUE_CHANGED, nullptr);
}

void UiController::initializeGeneratedUi() {
	ui_init();
	registerGeneratedCallbacks();
	refreshCarousel();
	lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);
}

void UiController::brightnessChangedCallback(lv_event_t *event) {
	UiController::instance().handleBrightnessChanged(event);
}

bool UiController::usesLvglSymbol(CarouselIcon icon) {
	return icon == CarouselIcon::Battery || icon == CarouselIcon::Solar || icon == CarouselIcon::Load || icon == CarouselIcon::WiFi;
}

const char *UiController::telemetrySymbol(CarouselIcon icon) {
	if (icon == CarouselIcon::Battery) return LV_SYMBOL_BATTERY_FULL;
	if (icon == CarouselIcon::Solar) return LV_SYMBOL_CHARGE;
	if (icon == CarouselIcon::WiFi) return LV_SYMBOL_WIFI;
	return LV_SYMBOL_POWER;
}

const void *UiController::iconAsset(CarouselIcon icon, bool selected) {
	if (icon == CarouselIcon::Temperature) return selected ? &ui_img_icon_temp_602_png : &ui_img_icon_temp_601_png;
	return selected ? &ui_img_icon_light_602_png : &ui_img_icon_light_601_png;
}

void UiController::initializeCarouselSymbols() {
	for (size_t index = 0; index < 3; ++index) {
		if (carouselSymbolLabels_[index]) continue;
		carouselSymbolLabels_[index] = lv_label_create(ui_Screen1);
		lv_obj_set_align(carouselSymbolLabels_[index], LV_ALIGN_CENTER);
		lv_obj_set_x(carouselSymbolLabels_[index], kCarouselSymbolPositions[index]);
		lv_obj_set_y(carouselSymbolLabels_[index], -9);
		lv_obj_set_style_text_font(carouselSymbolLabels_[index], &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_add_flag(carouselSymbolLabels_[index], LV_OBJ_FLAG_HIDDEN);
	}
}

void UiController::setCarouselIcon(lv_obj_t *image, lv_obj_t *symbolLabel, const CarouselItem &item, bool selected) {
	lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
	if (usesLvglSymbol(item.icon)) {
		lv_label_set_text(symbolLabel, telemetrySymbol(item.icon));
		lv_obj_set_style_text_color(symbolLabel, selected ? lv_color_hex(0x00FFFF) : lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_remove_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	if (selected) lv_obj_set_style_bg_image_src(image, iconAsset(item.icon, true), LV_PART_MAIN | LV_STATE_DEFAULT);
	else lv_image_set_src(image, iconAsset(item.icon, false));
}

void UiController::hideCarouselSlot(lv_obj_t *whiteIcon, lv_obj_t *whiteLabel, lv_obj_t *blueIcon, lv_obj_t *blueLabel) {
	lv_obj_add_flag(whiteIcon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(whiteLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(blueIcon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(blueLabel, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showPreview(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex) {
	const CarouselItem &item = carouselItem(itemIndex);
	setCarouselIcon(icon, symbolLabel, item, false);
	lv_label_set_text(label, item.carouselTitle);
	if (!usesLvglSymbol(item.icon)) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showSelection(lv_obj_t *icon, lv_obj_t *label, lv_obj_t *symbolLabel, size_t itemIndex) {
	const CarouselItem &item = carouselItem(itemIndex);
	setCarouselIcon(icon, symbolLabel, item, true);
	lv_label_set_text(label, item.carouselTitle);
	if (!usesLvglSymbol(item.icon)) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}

void UiController::refreshCarousel() {
	if (carouselItemCount_ == 0) return;
	initializeCarouselSymbols();
	for (lv_obj_t *symbolLabel : carouselSymbolLabels_) lv_obj_add_flag(symbolLabel, LV_OBJ_FLAG_HIDDEN);
	hideCarouselSlot(ui_volumeWhite, ui_volumeTextWhite, ui_volumeBlue, ui_volumeTextBlue);
	hideCarouselSlot(ui_tempWhite, ui_tempTextWhite, ui_tempBlue, ui_tempTextBlue);
	hideCarouselSlot(ui_lightWhite, ui_lightTextWhite, ui_lightBlue, ui_lightTextBlue);
	if (selectedItemIndex_ > 0) showPreview(ui_volumeWhite, ui_volumeTextWhite, carouselSymbolLabels_[0], selectedItemIndex_ - 1);
	showSelection(ui_tempBlue, ui_tempTextBlue, carouselSymbolLabels_[1], selectedItemIndex_);
	if (selectedItemIndex_ + 1 < carouselItemCount_) {
		showPreview(ui_lightWhite, ui_lightTextWhite, carouselSymbolLabels_[2], selectedItemIndex_ + 1);
	}
}

void UiController::handleRotation(bool clockwise) {
	if (carouselItemCount_ == 0) return;
	recordUserActivity();
	if (lv_screen_active() == ui_Screen4) {
		const int brightness = constrain(lv_arc_get_value(ui_lightArc) + (clockwise ? 5 : -5), 0, 100);
		lv_arc_set_value(ui_lightArc, brightness);
		lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, nullptr);
		return;
	}
	if (lv_screen_active() != ui_Screen1) return;
	moveSelection(clockwise);
	refreshCarousel();
}

void UiController::openSelectedScreen() {
	if (carouselItemCount_ == 0 || lv_screen_active() != ui_Screen1) return;
	const CarouselItem &item = selectedItem();
	if (item.controlsBrightness) _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen4_screen_init);
	else if (item.usesTemperatureScreen) _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen3_screen_init);
	else _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen2_screen_init);
	refreshActiveDetail();
}

void UiController::returnToCarousel() {
	if (lv_screen_active() == ui_Screen1) return;
	_ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen1_screen_init);
	refreshCarousel();
}

void UiController::formatTelemetryValue(const CarouselItem &item, char *text, size_t textSize) const {
	if (!hasTelemetrySnapshot_ || item.telemetryIndex == kNoTelemetryIndex || !latestSnapshot_.values[item.telemetryIndex].available) {
		strlcpy(text, "--", textSize); return;
	}
	const char *unit = item.usesTemperatureScreen ? AppConfig::instance().temperatureUnit : item.unit;
	char compactUnit[12] = {}; float value = latestSnapshot_.values[item.telemetryIndex].value;
	if (item.compact && fabsf(value) >= 1000.0F) { value /= 1000.0F; snprintf(compactUnit, sizeof(compactUnit), "k%s", unit); unit = compactUnit; }
	snprintf(text, textSize, "%.*f%s", item.precision, value, unit);
}

const lv_font_t *UiController::fittingDialFont(const char *text, uint8_t preferredSize, lv_coord_t width) {
	const lv_font_t *fonts[] = {&lv_font_montserrat_40, &lv_font_montserrat_28, &lv_font_montserrat_20}; const uint8_t sizes[] = {40, 28, 20};
	for (size_t index = 0; index < 3; ++index) { if (sizes[index] > preferredSize) continue; lv_point_t size = {}; lv_text_get_size(&size, text, fonts[index], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE); if (size.x <= width) return fonts[index]; }
	return &lv_font_montserrat_20;
}

void UiController::setDialValue(lv_obj_t *label, const char *text, uint8_t size) {
	lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP); lv_obj_set_style_text_font(label, fittingDialFont(text, size, lv_obj_get_width(label)), LV_PART_MAIN | LV_STATE_DEFAULT); lv_label_set_text(label, text);
}

void UiController::updateTelemetryArc(lv_obj_t *arc, const CarouselItem &item) const {
	lv_arc_set_range(arc, item.arcMinimum, item.arcMaximum); int value = item.arcMinimum;
	if (hasTelemetrySnapshot_ && item.telemetryIndex != kNoTelemetryIndex && latestSnapshot_.values[item.telemetryIndex].available) value = constrain(static_cast<int>(lroundf(latestSnapshot_.values[item.telemetryIndex].value)), item.arcMinimum, item.arcMaximum);
	lv_arc_set_value(arc, value);
}

void UiController::refreshActiveDetail() {
	if (carouselItemCount_ == 0) return;
	const CarouselItem &item = selectedItem();
	if (item.controlsBrightness) return;
	if (item.showsWiFiInfo) {
		if (activeRenderer_) activeRenderer_->hide();
		activeRenderer_ = nullptr;
		refreshWiFiInfo();
		return;
	}
	DetailRenderer *renderer = &rendererFactory_.rendererFor(item);
	// The generated detail screen is shared, so only one renderer may expose its
	// retained LVGL objects at a time.
	if (activeRenderer_ && activeRenderer_ != renderer) activeRenderer_->hide();
	activeRenderer_ = renderer;
	renderer->render(item, latestSnapshot_, hasTelemetrySnapshot_);
}

void UiController::synchronizeActiveScreen() {
	lv_obj_t *screen = lv_screen_active(); if (screen == lastActiveScreen_) return; lastActiveScreen_ = screen;
	if (screen == ui_Screen1) refreshCarousel(); else if (screen == ui_Screen2 || screen == ui_Screen3) refreshActiveDetail();
}

void UiController::update() {
	InputAction action; while (ElecrowCrowPanelInput::instance().readAction(action)) { recordUserActivity(); if (action.type == InputActionType::RotateClockwise) handleRotation(true); else if (action.type == InputActionType::RotateCounterclockwise) handleRotation(false); else if (action.type == InputActionType::Click) openSelectedScreen(); else returnToCarousel(); }
	TelemetrySnapshot snapshot; if (NetworkController::instance().copyLatestSnapshot(snapshot) && acceptTelemetrySnapshot(snapshot)) { Serial.printf("[INFO] Telemetry snapshot %lu received\n", static_cast<unsigned long>(snapshot.sequence)); if (lv_screen_active() == ui_Screen2 || lv_screen_active() == ui_Screen3) refreshActiveDetail(); }
	if (carouselItemCount_ > 0 && lv_screen_active() == ui_Screen2 && selectedItem().showsWiFiInfo && shouldRefreshWiFiInfo()) refreshWiFiInfo();
	updateDisplaySleep(); lv_timer_handler(); synchronizeActiveScreen(); delay(5);
}

size_t UiController::carouselItemCount() const { return carouselItemCount_; }

const CarouselItem &UiController::carouselItem(size_t index) const { return carouselItems_[index]; }

const CarouselItem &UiController::selectedItem() const { return carouselItems_[selectedItemIndex_]; }

size_t UiController::selectedItemIndex() const { return selectedItemIndex_; }

void UiController::moveSelection(bool clockwise) {
	if (clockwise && selectedItemIndex_ + 1 < carouselItemCount_) ++selectedItemIndex_;
	if (!clockwise && selectedItemIndex_ > 0) --selectedItemIndex_;
}

bool UiController::acceptTelemetrySnapshot(const TelemetrySnapshot &snapshot) {
	if (snapshot.sequence == lastTelemetrySequence_) return false;
	latestSnapshot_ = snapshot;
	lastTelemetrySequence_ = snapshot.sequence;
	hasTelemetrySnapshot_ = true;
	// Append exactly once per network sequence on the LVGL loop thread. The
	// history store is RAM-only and cannot contribute to flash wear.
	TelemetryHistory::instance().append(snapshot);
	return true;
}

bool UiController::hasTelemetrySnapshot() const { return hasTelemetrySnapshot_; }

const TelemetrySnapshot &UiController::latestSnapshot() const { return latestSnapshot_; }

bool UiController::shouldRefreshWiFiInfo() {
	const uint32_t now = millis();
	if (now - lastWiFiInfoRefreshMilliseconds_ < 1000U) return false;
	lastWiFiInfoRefreshMilliseconds_ = now;
	return true;
}

void UiController::initializeWiFiInfoLabel() {
	if (wifiInfoDetailLabel_) return;
	wifiInfoDetailLabel_ = lv_label_create(ui_Screen2);
	lv_obj_set_width(wifiInfoDetailLabel_, 180);
	lv_obj_set_align(wifiInfoDetailLabel_, LV_ALIGN_CENTER);
	lv_obj_set_y(wifiInfoDetailLabel_, 52);
	lv_obj_set_style_text_align(wifiInfoDetailLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(wifiInfoDetailLabel_, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(wifiInfoDetailLabel_, &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UiController::initializeWiFiSignalLabel() {
	if (wifiSignalDetailLabel_) return;
	wifiSignalDetailLabel_ = lv_label_create(ui_Screen2);
	lv_obj_set_width(wifiSignalDetailLabel_, 150);
	lv_obj_set_height(wifiSignalDetailLabel_, 56);
	lv_obj_set_align(wifiSignalDetailLabel_, LV_ALIGN_CENTER);
	lv_obj_set_x(wifiSignalDetailLabel_, 4);
	lv_obj_set_y(wifiSignalDetailLabel_, 3);
	lv_obj_set_style_text_align(wifiSignalDetailLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_color(wifiSignalDetailLabel_, lv_color_hex(0x33DCFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(wifiSignalDetailLabel_, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UiController::refreshWiFiInfo() {
	const bool hotspot = (WiFi.getMode() & WIFI_MODE_AP) != 0 && WiFi.softAPgetStationNum() >= 0 &&
							 WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
	const bool connected = !hotspot && WiFi.status() == WL_CONNECTED;
	char signalText[16];
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

	lv_obj_set_style_bg_image_src(ui_Screen2, &ui_img_v2_bj_volume_100_png, LV_PART_MAIN | LV_STATE_DEFAULT);
	for (lv_obj_t *label : electricalTickLabels_) {
		if (label) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
	}
	lv_label_set_text(ui_Label4, "WiFi Info");
	lv_obj_add_flag(ui_VolNum, LV_OBJ_FLAG_HIDDEN);
	initializeWiFiSignalLabel();
	lv_label_set_text(wifiSignalDetailLabel_, signalText);
	lv_arc_set_range(ui_VolumeArc, 0, 100);
	lv_arc_set_value(ui_VolumeArc, signalPercent);
	lv_obj_remove_flag(ui_VolumeArc, LV_OBJ_FLAG_CLICKABLE);
	lv_image_set_src(ui_Image3, nullptr);
	lv_obj_add_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
	initializeWiFiInfoLabel();
	if (hotspot) {
		lv_label_set_text_fmt(wifiInfoDetailLabel_, "Hotspot: %s\nIP: %s", WiFi.softAPSSID().c_str(),
							  WiFi.softAPIP().toString().c_str());
	} else if (connected) {
		const String ipAddress = WiFi.localIP().toString();
		lv_label_set_text_fmt(wifiInfoDetailLabel_, "SSID: %s\nIP: %s", WiFi.SSID().c_str(), ipAddress.c_str());
	} else {
		lv_label_set_text(wifiInfoDetailLabel_, "SSID: --\nIP: --");
	}
	lv_obj_set_y(ui_screen2ReturnBt, 85);
	lv_obj_set_y(ui_Label3, 84);
	lv_obj_remove_flag(ui_screen2ReturnBt, LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(ui_Label3, LV_OBJ_FLAG_HIDDEN);
}

void UiController::updateElectricalTickLabels() {
	const CarouselItem &item = selectedItem();
	updateTelemetryTickLabels(ui_Screen2, electricalTickLabels_, item, item.unit, lv_color_hex(item.tickLabelColor));
}

void UiController::updateTemperatureTickLabels() {
	const CarouselItem &item = selectedItem();
	updateTelemetryTickLabels(ui_Screen3, temperatureTickLabels_, item, AppConfig::instance().temperatureUnit,
							  lv_color_hex(item.tickLabelColor));
}

void UiController::updateTelemetryTickLabels(lv_obj_t *screen, lv_obj_t *labels[], const CarouselItem &item,
																								 const char *unit, lv_color_t color) {
	for (size_t index = 0; index < kDialTickLabelCount; ++index) {
		if (!labels[index]) {
			labels[index] = lv_label_create(screen);
			lv_obj_set_size(labels[index], 42, 14);
			lv_obj_set_style_text_align(labels[index], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
			lv_obj_set_style_text_font(labels[index], &lv_font_montserrat_10, LV_PART_MAIN | LV_STATE_DEFAULT);
		}
		const float fraction = static_cast<float>(index) / static_cast<float>(kDialTickLabelCount - 1);
		const int value = static_cast<int>(lroundf(item.arcMinimum + fraction * (item.arcMaximum - item.arcMinimum)));
		const float angle = kDialTickStartAngleRadians + fraction * kDialTickSweepRadians;
		const int x = static_cast<int>(lroundf(kDialCenterX + kDialTickLabelRadius * cosf(angle))) - 21;
		const int y = static_cast<int>(lroundf(kDialCenterY + kDialTickLabelRadius * sinf(angle))) - 7;
		char text[14];
		snprintf(text, sizeof(text), "%d%s", value, unit);
		lv_label_set_text(labels[index], text);
		lv_obj_set_pos(labels[index], x, y);
		lv_obj_set_style_text_color(labels[index], color, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_remove_flag(labels[index], LV_OBJ_FLAG_HIDDEN);
	}
}

void UiController::recordUserActivity() {
	lastUserActivityMilliseconds_ = millis();
	if (!displaySleeping_) return;
	displaySleeping_ = false;
	display_->setBacklightEnabled(true);
	Serial.println("[INFO] Display backlight restored by user activity");
}

void UiController::updateDisplaySleep() {
	if (displaySleeping_ || sleepAfterSeconds_ == 0) return;
	const uint32_t sleepAfterMilliseconds = static_cast<uint32_t>(sleepAfterSeconds_) * 1000U;
	if (millis() - lastUserActivityMilliseconds_ < sleepAfterMilliseconds) return;
	displaySleeping_ = true;
	display_->setBacklightEnabled(false);
	Serial.println("[INFO] Display backlight sleeping after inactivity");
}

void UiController::handleBrightnessChanged(lv_event_t *event) {
	if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
	const int brightness = constrain(lv_arc_get_value(lv_event_get_target_obj(event)), 0, 100);
	brightnessPercent_ = static_cast<uint8_t>(brightness);
	char label[8];
	snprintf(label, sizeof(label), "%d%%", brightness);
	lv_label_set_text(ui_LightNum, label);
	if (!displaySleeping_) display_->setBacklightPercent(brightnessPercent_);
}