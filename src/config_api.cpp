#include "config_api.h"

#include <Arduino.h>
#include <Esp.h>
#include <SPIFFS.h>

#include "constants.h"

namespace {

using rv_control_ui::constants::kConfigApiPort;
using rv_control_ui::constants::kConfigApiRestartDelayMilliseconds;
using rv_control_ui::constants::kConfigFilePath;
using rv_control_ui::constants::kConfigTempFilePath;
using rv_control_ui::constants::kMaximumConfigBytes;

}  // namespace

void ConfigApi::begin(ConfigStore &store, const AppConfig &config, const DisplayCatalog &catalog) {
  store_ = &store;
  config_ = &config;
  catalog_ = &catalog;
  restartPending_ = false;
  restartAtMilliseconds_ = 0;
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  server_.on("/api/config", HTTP_POST, [this]() { handlePostConfig(); });
  server_.begin();
  Serial.printf("[INFO] Configuration API listening on port %u\n", static_cast<unsigned>(kConfigApiPort));
}

void ConfigApi::handleClient() { server_.handleClient(); }

void ConfigApi::serviceRestart() {
  if (restartPending_ && static_cast<int32_t>(millis() - restartAtMilliseconds_) >= 0) {
    Serial.println("[INFO] Restarting to apply new configuration");
    Serial.flush();
    ESP.restart();
  }
}

void ConfigApi::handleGetConfig() {
  JsonDocument document;
  store_->toJson(*config_, *catalog_, document);
  String body;
  serializeJson(document, body);
  server_.send(200, "application/json", body);
}

void ConfigApi::handlePostConfig() {
  const String body = server_.arg("plain");
  if (body.length() == 0 || body.length() > kMaximumConfigBytes) {
    sendError(400, "request body has an invalid size");
    return;
  }

  JsonDocument document;
  if (deserializeJson(document, body)) {
    sendError(400, "request body is malformed JSON");
    return;
  }

  // Validate with the same rules the boot path applies; never persist an
  // invalid document, so a bad POST cannot wedge the next boot.
  AppConfig candidateConfig = {};
  DisplayCatalog candidateCatalog = {};
  char validationError[128] = {};
  if (!store_->validateDocument(document, candidateConfig, candidateCatalog, validationError, sizeof(validationError))) {
    sendError(400, validationError[0] ? validationError : "configuration failed validation");
    return;
  }

  if (!writeConfigAtomically(body)) {
    sendError(500, "could not write configuration to storage");
    return;
  }

  server_.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"restarting\"}");
  Serial.println("[INFO] Configuration updated via API; restart pending");
  restartPending_ = true;
  restartAtMilliseconds_ = millis() + kConfigApiRestartDelayMilliseconds;
}

void ConfigApi::sendError(int statusCode, const char *message) {
  JsonDocument document;
  document["error"] = message;
  String body;
  serializeJson(document, body);
  server_.send(statusCode, "application/json", body);
  Serial.printf("[WARN] Configuration API rejected request: %s\n", message);
}

bool ConfigApi::writeConfigAtomically(const String &body) {
  File file = SPIFFS.open(kConfigTempFilePath, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.print(body);
  file.close();
  if (written != body.length()) {
    SPIFFS.remove(kConfigTempFilePath);
    return false;
  }
  SPIFFS.remove(kConfigFilePath);
  if (!SPIFFS.rename(kConfigTempFilePath, kConfigFilePath)) {
    SPIFFS.remove(kConfigTempFilePath);
    return false;
  }
  return true;
}
