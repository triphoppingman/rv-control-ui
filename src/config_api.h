#pragma once

#include <WebServer.h>

#include "config_loader.h"
#include "constants.h"

/**
 * @brief Snapshot of network state reported by the info endpoint.
 *
 * Populated by NetworkController on each info request so the API never reads
 * Wi-Fi/MQTT state directly. Pure data; no secrets beyond the connected SSID.
 */
struct NetworkStatus {
  bool hotspotActive;
  bool wifiConnected;
  bool mqttConnected;
  char wifiSsid[33];
  char ipAddress[16];
  int rssi;
};

/**
 * @brief Minimal HTTP REST API for configuration, status, and restart.
 *
 * The server is owned and pumped by NetworkController, which runs the network
 * FreeRTOS task; this class never touches LVGL. It exposes the running
 * configuration (secrets included, by design), a debugging info snapshot, and a
 * restart action. A config POST is written atomically and applied by restart.
 */
class ConfigApi {
 public:
  /**
   * @brief Start the HTTP server and register the endpoints.
   *
   * @param store The configuration store used for validation and serialization.
   * @param config The running typed configuration returned by GET.
   * @param catalog The running typed catalog returned by GET.
   */
  void begin(ConfigStore &store, const AppConfig &config, const DisplayCatalog &catalog);

  /** @brief Service one pending HTTP client; call on every network-task pass. */
  void handleClient();

  /** @brief Restart the device once the grace delay has elapsed; call every pass. */
  void serviceRestart();

  /** @brief Update the network status snapshot reported by the info endpoint. */
  void setNetworkStatus(const NetworkStatus &status);

 private:
  /** @brief Return the running configuration as JSON, secrets included. */
  void handleGetConfig();

  /** @brief Validate, atomically write, and restart on the buffered replacement config. */
  void handlePostConfig();

  /** @brief Return a build/device/network status snapshot for debugging. */
  void handleGetInfo();

  /** @brief Acknowledge and schedule an immediate device restart. */
  void handlePostRestart();

  /** @brief Accumulate a raw POST body chunk; called by the WebServer during parsing. */
  void collectPostBody();

  /** @brief Respond with a concise JSON error object and status code. */
  void sendError(int statusCode, const char *message);

  /** @brief Write a validated JSON body to /config.json via a temp file and rename. */
  bool writeConfigAtomically(const String &body);

  WebServer server_;
  ConfigStore *store_;
  const AppConfig *config_;
  const DisplayCatalog *catalog_;
  bool restartPending_;
  uint32_t restartAtMilliseconds_;
  NetworkStatus networkStatus_ = {};

  // Length/overflow for the shared static POST buffer; the buffer itself lives in
  // BSS (see config_api.cpp) so it never consumes the network task's stack.
  size_t postBodyLength_;
  bool postBodyOverflow_;
};
