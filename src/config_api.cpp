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

// The WebServer handles one client at a time, so a single shared buffer is safe. It
// lives in BSS rather than on the ConfigApi object so the large buffer never sits on
// the network task's stack (which previously triggered a stack-canary watchdog reset).
char gPostBody[kMaximumConfigBytes + 1];

}  // namespace

void ConfigApi::begin(ConfigStore &store, const AppConfig &config, const DisplayCatalog &catalog) {
  store_ = &store;
  config_ = &config;
  catalog_ = &catalog;
  restartPending_ = false;
  restartAtMilliseconds_ = 0;
  postBodyLength_ = 0;
  postBodyOverflow_ = false;
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  // The 4-argument form supplies the raw-body callback so a JSON POST body is
  // received through HTTPRaw chunks rather than the (empty) plain argument.
  server_.on(
      "/api/config", HTTP_POST, [this]() { handlePostConfig(); }, [this]() { collectPostBody(); });
  server_.on("/api/info", HTTP_GET, [this]() { handleGetInfo(); });
  server_.on("/api/restart", HTTP_POST, [this]() { handlePostRestart(); });
  server_.begin();
  Serial.printf("[INFO] Configuration API listening on port %u\n", static_cast<unsigned>(kConfigApiPort));
}

void ConfigApi::handleClient() { server_.handleClient(); }

void ConfigApi::setNetworkStatus(const NetworkStatus &status) { networkStatus_ = status; }

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

void ConfigApi::collectPostBody() {
  HTTPRaw &raw = server_.raw();
  if (raw.status == RAW_START) {
    postBodyLength_ = 0;
    postBodyOverflow_ = false;
    return;
  }
  if (raw.status == RAW_WRITE || raw.status == RAW_END) {
    for (size_t index = 0; index < raw.currentSize; ++index) {
      if (postBodyLength_ < kMaximumConfigBytes) {
        gPostBody[postBodyLength_++] = static_cast<char>(raw.buf[index]);
      } else {
        postBodyOverflow_ = true;
      }
    }
    gPostBody[postBodyLength_] = '\0';
  }
}

void ConfigApi::handlePostConfig() {
  if (postBodyOverflow_ || postBodyLength_ == 0) {
    sendError(400, "request body has an invalid size");
    return;
  }
  const String body(gPostBody);

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

void ConfigApi::handleGetInfo() {
  JsonDocument document;
  JsonObject build = document["build"].to<JsonObject>();
  build["date"] = __DATE__;
  build["time"] = __TIME__;

  JsonObject device = document["device"].to<JsonObject>();
  char mac[18];
  snprintf(mac, sizeof(mac), "%012llX", static_cast<unsigned long long>(ESP.getEfuseMac()));
  device["mac"] = mac;
  device["chip_model"] = ESP.getChipModel();
  device["chip_revision"] = ESP.getChipRevision();
  device["cpu_mhz"] = ESP.getCpuFreqMHz();
  device["uptime_seconds"] = millis() / 1000;
  device["free_heap"] = ESP.getFreeHeap();
  device["sketch_size"] = ESP.getSketchSize();
  device["free_sketch_space"] = ESP.getFreeSketchSpace();

  JsonObject network = document["network"].to<JsonObject>();
  network["hotspot_active"] = networkStatus_.hotspotActive;
  network["wifi_connected"] = networkStatus_.wifiConnected;
  network["mqtt_connected"] = networkStatus_.mqttConnected;
  network["ssid"] = networkStatus_.wifiSsid;
  network["ip"] = networkStatus_.ipAddress;
  network["rssi"] = networkStatus_.rssi;

  String body;
  serializeJson(document, body);
  server_.send(200, "application/json", body);
}

void ConfigApi::handlePostRestart() {
  server_.send(200, "application/json", "{\"status\":\"ok\",\"action\":\"restarting\"}");
  Serial.println("[INFO] Restart requested via API");
  restartPending_ = true;
  restartAtMilliseconds_ = millis() + kConfigApiRestartDelayMilliseconds;
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
