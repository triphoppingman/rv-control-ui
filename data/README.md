# data/ — device configuration

This directory holds the device-local runtime configuration that PlatformIO
packages into the SPIFFS filesystem image.

- **`config.json`** (gitignored) — the device configuration: Wi-Fi, MQTT,
  display, logging, and the telemetry catalog. Create it by copying the tracked
  [`config-example.json`](../config-example.json) and editing in your local
  values.

  ```sh
  cp ../config-example.json config.json
  ```

- Apply it with `~/.platformio/penv/bin/pio run --target uploadfs`, or update a
  running device over the network with `POST /api/config`.

This file is device-local and contains credentials — do not commit it.

See the [root README](../README.md) for the full configuration schema, the
setup hotspot, the configuration REST API, and the status LED.
