#include "application.h"

#include <Arduino.h>

#include "config_loader.h"
#include "elecrow_crowpanel_display.h"
#include "elecrow_crowpanel_input.h"
#include "network_controller.h"
#include "ui_controller.h"
#include <esp_ota_ops.h>

#include "constants.h"

extern "C" bool verifyRollbackLater() { return true; }

Application &Application::instance() {
	static Application application;
	return application;
}

void Application::begin() {
	Serial.begin(115200);
	delay(100);
	Serial.println("[INFO] RV Control UI booting");
	char configurationError[128] = {};
	AppConfig &config = AppConfig::instance();
	DisplayCatalog &catalog = DisplayCatalog::instance();
	const bool configurationLoaded = ConfigStore::instance().load(config, catalog, configurationError, sizeof(configurationError));
	if (configurationLoaded) {
		Serial.printf("[INFO] Configuration loaded; MQTT host=%s base topic=%s items=%u\n", config.mqttHost,
						  config.mqttBaseTopic, static_cast<unsigned>(catalog.itemCount));
	} else {
		Serial.printf("[ERROR] Configuration unavailable: %s\n", configurationError);
	}
	if (!ElecrowCrowPanelDisplay::instance().begin()) {
		rejectPendingOta();
		return;
	}
	UiController::instance().begin();
	UiController::instance().configure();
	if (!UiController::instance().initializeCarousel()) {
		Serial.println("[ERROR] UI display allocation failed");
		rejectPendingOta();
		return;
	}
	ElecrowCrowPanelInput::instance().begin();
	UiController::instance().initializeGeneratedUi();
	if (!NetworkController::instance().begin()) {
		Serial.println("[ERROR] Network services unavailable; local UI remains active");
	}
	initialized_ = true;
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	otaPendingVerification_ = running &&
		esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
	otaHealthStartedAtMilliseconds_ = millis();
}

void Application::update() {
	if (!initialized_) return;
	UiController::instance().update();
	serviceOtaHealth();
}

void Application::rejectPendingOta() {
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
		Serial.println("[ERROR] New firmware failed critical startup; rolling back");
		Serial.flush();
		esp_ota_mark_app_invalid_rollback_and_reboot();
	}
}

void Application::serviceOtaHealth() {
	if (!otaPendingVerification_ || millis() - otaHealthStartedAtMilliseconds_ <
										 rv_control_ui::constants::kOtaBootHealthyMilliseconds) return;
	const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
	if (result == ESP_OK) {
		otaPendingVerification_ = false;
		Serial.println("[INFO] New firmware passed local health checks; rollback cancelled");
	} else {
		Serial.printf("[ERROR] Could not confirm OTA firmware: %s\n", esp_err_to_name(result));
	}
}