#pragma once

#include <WebServer.h>

#include "config_loader.h"

/**
 * @brief Minimal HTTP REST API for reading and replacing /config.json.
 *
 * The server is owned and pumped by NetworkController, which runs the network
 * FreeRTOS task; this class never touches LVGL. It exposes the running
 * configuration (secrets included, by design) and accepts a validated
 * replacement that is written atomically and applied by a device restart.
 */
class ConfigApi {
 public:
  /**
   * @brief Start the HTTP server and register the configuration endpoints.
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

 private:
  /** @brief Return the running configuration as JSON, secrets included. */
  void handleGetConfig();

  /** @brief Validate, atomically write, and restart on a replacement configuration. */
  void handlePostConfig();

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
};
