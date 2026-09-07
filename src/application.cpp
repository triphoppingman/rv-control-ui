#include "application.h"

#include <Arduino.h>

#include "config_loader.h"
#include "elecrow_crowpanel_display.h"
#include "elecrow_crowpanel_input.h"
#include "network_controller.h"
#include "ui_controller.h"

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
	if (!ElecrowCrowPanelDisplay::instance().begin()) return;
	UiController::instance().begin();
	UiController::instance().configure();
	UiController::instance().initializeCarousel();
	ElecrowCrowPanelInput::instance().begin();
	UiController::instance().initializeGeneratedUi();
	NetworkController::instance().begin();
	initialized_ = true;
}

void Application::update() {
	if (!initialized_) return;
	UiController::instance().update();
}