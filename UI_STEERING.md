# RV Control UI Steering Document

## Goal

Evolve this PlatformIO firmware into a local, read-only display for RV telemetry. The first release is a browsable Renogy solar and battery dashboard on the 240 x 240 rotary screen, fed by the local `rv-control` MQTT service.

It must remain usable when telemetry, Wi-Fi, or MQTT is unavailable. This effort must not change the `rv-control` service MQTT contract or enable device write-back.

## Existing Foundation

- Target: ESP32-S3 Elecrow 1.28-inch round display with 240 x 240 GC9A01 panel.
- Runtime: Arduino/PlatformIO, LVGL 9.1, LovyanGFX, CST816D touch, and a rotary encoder.
- Application shell: `Arduino/RotaryScreen_1_28/RotaryScreen_1_28.ino` initializes hardware, owns the LVGL loop, and dispatches queued encoder actions.
- SquareLine source: `Arduino/ui_project/SLS_1_28v2-20250807/`.
- Generated SquareLine export used at build time: `Arduino/libraries/UI/`.
- Example backend topic: `rv-control` publishes Renogy JSON on `rv/renogy`, constructed from `[mqtt] base_topic` and the Renogy source `topic`.

The existing SquareLine screens demonstrate the display and input hardware. Replace their volume and temperature semantics as telemetry views are introduced, but preserve the proven display, touch, encoder queue, LVGL loop, and backlight setup unless a concrete issue requires change. Retain the existing brightness screen as a functional local control.

The current SquareLine asset library is the required visual baseline for the Renogy UI. Reuse its boot graphics, circular backgrounds, selected/unselected carousel treatments, bar graphics, and brightness imagery wherever they fit. Adapt the existing value and icon slots to Renogy labels and values instead of replacing the visual language with a new dashboard design. New artwork is justified only when the existing assets cannot clearly distinguish a Renogy measurement; it must match the established scale, contrast, and round-screen composition.

## Configuration Is External

Wi-Fi, MQTT, topic, catalog, and display preferences are loaded from a single `/config.json` in SPIFFS. They must never be embedded in firmware source or printed in full to serial logs. A device-specific `config.json` contains credentials, so it must not be committed, included in screenshots, or copied into documentation.

Use this configuration arrangement:

| Item | Version control | Purpose |
| --- | --- | --- |
| `data/config.json` | ignored | Device-specific configuration (settings + catalog) uploaded into the SPIFFS image by PlatformIO. |
| `config-example.json` | tracked | Complete JSON schema with safe placeholder values. |
| `partitions.csv` | tracked | Custom partition table with a dedicated, bounded SPIFFS partition. |
| `src/config_loader.*` | tracked | Mounts SPIFFS, parses and validates `/config.json`, and exposes typed settings + catalog with redacted diagnostics. |

Configure PlatformIO to use `partitions.csv` and upload the filesystem with `pio run --target uploadfs` before (or separately from) firmware upload. The custom table allocates a bounded SPIFFS partition: substantially more than the few kilobytes needed for the configuration file, while leaving nearly all of the 16 MB flash for firmware and future application needs. Do not use SPIFFS for history, logs, generated assets, or telemetry caching in the first release.

The loader sets an explicit maximum configuration file size, reads the file once during startup, rejects malformed JSON, rejects missing required keys, and bounds all parsed string values before passing them to Wi-Fi or MQTT libraries. The whole document is validated and rejected as a unit; no partially applied configuration is produced. The identical validator is reused by the configuration REST API so a POSTed configuration can never be persisted unless it would boot. Configuration changes take effect only after reboot or an intentional reload action.

The `catalog` section of `/config.json` defines the MQTT-backed carousel entries, using the same `sources`, `palettes`, and `items` keys and limits as the previous standalone catalog. Reject the whole catalog on an invalid entry; do not create a partially configured carousel. Brightness and Wi-Fi Info remain hardcoded local items after the catalog entries, because they are not MQTT telemetry.

An empty `wifi.ssid` is valid and means no station network is configured; the device then runs its setup hotspot. A validated REST API (`GET`/`POST /api/config`) is always exposed — on the hotspot address `192.168.77.1` and on the station LAN address — so the device can be reconfigured without reprogramming; a successful POST is written atomically and applied by restart. See [docs/features/json-config-hotspot-api.md](docs/features/json-config-hotspot-api.md).

Schema example (not a location for real values):

```json
{
	"wifi": {"ssid": "YOUR_WIFI_NETWORK", "password": "YOUR_WIFI_PASSWORD"},
	"mqtt": {"host": "mqtt.local", "port": 1883, "username": "", "password": "", "base_topic": "rv"},
	"display": {"temperature_unit": "F", "expected_poll_interval_seconds": 60, "sleep_after_seconds": 300,
				"show_brightness": true, "show_wifi": true},
	"logging": {"serial_level": "INFO"},
	"catalog": {"sources": [], "palettes": [], "items": []}
}
```

Construct the subscription from slash-trimmed base and suffix values, yielding `rv/renogy` with the defaults. The status screen may show the resulting topic, never a password or Wi-Fi SSID.

`sleep_after_seconds` controls the inactivity timeout for blanking the display backlight; `300` is five minutes and `0` disables automatic sleep. Any touch or rotary-encoder action restores the last selected brightness immediately.

## Serial Logging

Serial output is required for device setup and field diagnostics. Initialize `Serial` before mounting SPIFFS and emit timestamped, levelled messages at boot, configuration load, Wi-Fi connection-state changes, MQTT connection/reconnection, subscription success, first valid snapshot, stale transitions, and recoverable errors.

Use the `[logging] serial_level` setting with `ERROR`, `WARN`, `INFO`, and `DEBUG` values; default to `INFO` when the setting is absent or invalid. `DEBUG` may report bounded payload metadata such as byte count and accepted field names, but must never print a raw configuration file, Wi-Fi SSID, password, MQTT username/password, or complete MQTT payload. Log broker host and subscription topic only when they do not disclose credentials.

Rate-limit repeating failures from Wi-Fi, MQTT, JSON parsing, and oversized messages. Log the first occurrence immediately, aggregate identical recurrence counts, and print a periodic summary while the condition persists. On recovery, print the prior failure count and elapsed duration. This keeps `pio device monitor` useful without allowing a disconnected broker to flood the serial buffer.

## Hardware Bring-Up and Troubleshooting

Bring the device up in this order and stop at the first stage that fails. Each stage has an independent serial signature, so skipping ahead usually misattributes the fault.

1. **USB and serial.** `~/.platformio/penv/bin/pio device list` must show the board. If nothing appears, try a USB-C *data* cable, another port, and confirm the user is in the `dialout` group on Linux (`id -nG`). A port that enumerates but never prints is usually a baud mismatch; the monitor must run at `115200`.
2. **Firmware upload.** If upload fails to sync, hold **BOOT**, tap **RESET**, release **BOOT**, then retry with an explicit port: `~/.platformio/penv/bin/pio run --target upload --upload-port /dev/ttyACM0`. Because `ARDUINO_USB_CDC_ON_BOOT=1`, the serial device disappears and re-enumerates across resets; reopen the monitor after upload instead of assuming the board hung.
3. **Filesystem image.** `/config.json` lives in SPIFFS, not firmware. Run `~/.platformio/penv/bin/pio run --target uploadfs` after any change to `data/`. A firmware-only upload leaves the previous configuration in place and produces stale or configuration-error behavior that looks like a code defect.
4. **Display and backlight.** A dark panel with healthy serial output is normally backlight or sleep behavior, not a driver fault. Check `[display] sleep_after_seconds` and confirm touch or encoder input restores the last brightness. Garbled or shifted pixels point at SPI2/DMA or panel setup; compare against the known-good `Arduino/RotaryScreen_1_28/RotaryScreen_1_28.ino` before editing driver code.
5. **Touch and encoder.** Touch uses `Wire1` on GPIO 6/7 and the encoder uses GPIO 45/42/41. Dead touch with a working encoder is an I2C/CST816D problem; dead encoder with working touch is a queue or pin problem. Never diagnose either by adding LVGL calls to a FreeRTOS task.
6. **Wi-Fi.** The ESP32-S3 radio is 2.4 GHz only; a 5 GHz-only SSID never associates. Read the logged `WiFi.status()` value: `1` = SSID not found (name, band, or hidden network), `4` = association/auth failure (usually the password), `5` = connection lost, `6` = disconnected/retrying, `3` = connected. Confirm the connected log line reports a plausible `RSSI` and a routable IP.
7. **MQTT.** Read the logged `PubSubClient` state: `-4` timeout, `-3` connection lost, `-2` TCP connect failed (host, port, firewall, or broker not listening on the LAN interface), `-1` clean disconnect, `4` bad credentials, `5` not authorized. Verify reachability from a workstation on the same network with `mosquitto_sub -h HOST -p 1883 -t 'rv/#' -v` before changing firmware.
8. **Topics and payloads.** Subscribed topics are built from `[mqtt] base_topic` plus each catalog `sources[].topic`. A connected client that never updates a value is almost always a topic-suffix or `value_key` mismatch against what `rv-control` actually publishes; compare the observed payload keys to the catalog before assuming a parser bug. `--` on one field with others valid is expected and correct.
9. **Freshness.** Values that appear then go stale indicate the collector stopped publishing, not a display fault. Confirm `rv-control` is still running its source and that `expected_poll_interval_seconds` matches the collector's real publish cadence.

Diagnostic guardrails:

- Raise `[logging] serial_level` to `DEBUG` for bring-up, then return it to `INFO`. `DEBUG` may report bounded payload metadata only; it must never print the raw INI file, SSID, passwords, or complete payloads.
- Never paste `data/config.json`, serial captures containing credentials, or real device identifiers into issues, commits, or documentation. Redact SSID, broker credentials, and MAC addresses.
- Reproduce a suspected hardware defect against the reference sketch behavior before altering the proven display, touch, encoder, DMA, or backlight setup.
- Prefer a full erase (`~/.platformio/penv/bin/pio run --target erase`) only as a deliberate last step; it removes the SPIFFS configuration and requires re-running `uploadfs`.
- Do not report Wi-Fi, MQTT, serial, or on-device results as verified unless the check was actually executed on hardware. State plainly which stages were run and which were not.

## First-Release Boundary

The display will:

- Connect to configured local Wi-Fi and a single MQTT broker.
- Subscribe to the configured Renogy topic.
- Validate each bounded JSON message and retain the newest valid snapshot.
- Display battery overview, solar/load detail, and system status screens.
- Support equivalent touch and encoder navigation.
- Render loading, reconnecting, stale, invalid-payload, and unavailable states.

Out of scope: MQTT commands, subscriptions to `/set`, BLE/CAN hardware access, direct Renogy connections, cloud telemetry, write-back, historical storage, multi-device aggregation, and alerts. Read-only behavior is a safety boundary.

## Renogy MQTT Data Contract

`rv-control` publishes a JSON object for each reading. Do not depend on JSON property order, retained messages, or fields that belong only to another Renogy device type. For `RNG_CTRL`, the first dashboard supports this subset:

```json
{
  "battery_percentage": 82,
  "battery_voltage": 13.1,
  "battery_current": 8.42,
  "battery_temperature": 72,
  "controller_temperature": 84,
  "pv_voltage": 20.7,
  "pv_current": 4.16,
  "load_voltage": 12.9,
  "load_current": 1.2
}
```

Voltage is V, current is A, percentage is percent, and the temperature unit comes from `RVUI_TEMPERATURE_UNIT` and must match the backend configuration. A missing, null, non-numeric, or non-finite field renders as `--` without invalidating the other fields. Reject a payload only when its root is not an object or it exceeds the configured message limit.

Use ArduinoJson or another structured parser. Do not extract values through string searching. Apply a fixed maximum message size and document capacity with modest headroom based on captured payloads; discard and count oversized data.

## Firmware Boundaries

| Area | Responsibility |
| --- | --- |
| `Arduino/RotaryScreen_1_28/RotaryScreen_1_28.ino` | Board setup, LVGL initialization, input dispatch, and application startup. |
| `data/config.json` | Ignored device-specific SPIFFS configuration image (settings + catalog). |
| `config-example.json` | Tracked placeholder schema for `/config.json`. |
| `partitions.csv` | Tracked 256 KiB SPIFFS partition allocation. |
| `src/config_loader.*` | SPIFFS mounting, bounded JSON parsing, validation, typed settings + catalog, and redacted diagnostics. |
| `src/network_controller.*` | `NetworkController`: Wi-Fi station/hotspot, MQTT reconnects, subscriptions, bounded receive callback, snapshot handoff, and ownership of the API and status LED. |
| `src/config_api.*` | `ConfigApi`: HTTP `WebServer` and the GET/POST `/api/config` handlers. |
| `src/status_led.*` | `StatusLed`: NeoPixel network status indicator. |
| `src/constants.h` | All compile-time constants, including hotspot, API, and status-LED values. |
| `src/ui_controller.*` | Screen state, navigation intents, and LVGL value/status updates. |
| `Arduino/ui_project/` | SquareLine layout source. |
| `Arduino/libraries/UI/` | Generated SquareLine output. Do not make irreproducible hand edits here. |

Use an MQTT subscription rather than periodic application-level polling. `rv-control` already publishes each fresh Renogy reading; subscribing promptly updates the display without the device issuing redundant requests or needing to infer the source poll schedule.

Run Wi-Fi and MQTT connection management in a dedicated FreeRTOS task. That task owns broker reconnects, calls the MQTT client's maintenance loop, receives messages, parses bounded JSON, and atomically replaces the latest telemetry snapshot or posts a compact snapshot-ready notification. It must use bounded work and backoff delays while disconnected.

The MQTT callback and network task must never touch LVGL. `loop()` is the only place that changes LVGL objects: it consumes the newest snapshot, refreshes the selected value, and calls `lv_timer_handler()`. This preserves the existing encoder queue pattern and keeps the display responsive during Wi-Fi loss and broker reconnects.

## UI Model

Create SquareLine layouts for the round screen using the existing generated asset vocabulary. Preserve the established device-control background, blue selected state, white neighboring state, and existing bar/arc treatments wherever their meanings remain legible. Preserve the proven RotaryScreen interaction model: exactly one value is prominent at the center, and the rotary encoder moves a focused carousel across individual values. Neighboring values may appear as partially visible label/icon previews, just as the demo shows adjacent controls, but must not make the 240-pixel display feel like a table.

The initial carousel items, in order, are:

1. **Battery state of charge**: `battery_percentage` as the primary percentage.
2. **Battery voltage**: `battery_voltage` in V.
3. **Battery current**: `battery_current` in A, preserving its sign.
4. **Battery temperature**: `battery_temperature` using the configured temperature unit.
5. **Controller temperature**: `controller_temperature` using the configured temperature unit.
6. **PV voltage**: `pv_voltage` in V.
7. **PV current**: `pv_current` in A.
8. **Load voltage**: `load_voltage` in V.
9. **Load current**: `load_current` in A.
10. **Display brightness**: retain the existing local backlight control and its `lightArc` behavior. This is the sole non-telemetry carousel item.
11. **Connection status**: Wi-Fi/MQTT state, snapshot age, topic, and payload error count.

Do not make normal telemetry values editable. A single click may open a concise detail screen for the selected telemetry value, but must not modify it. Brightness is the exception: when its carousel item is selected, encoder rotation changes the local backlight in the existing bounded $0$ to $100$ percent range. A double click returns from a detail view to the carousel; it has no telemetry-side effect.

Touch must agree with encoder state: tapping a preview selects that item, and an optional swipe advances or reverses the same carousel index. Avoid a separate pager that changes the navigation order.

Format voltage to one decimal place and current to meaningful precision. Derived power may appear on a telemetry value's optional detail screen only when both operands are valid: $P = V \times I$. Mark data fresh for the configured expected poll interval plus transport tolerance; mark it stale after that, and unavailable before any valid payload. Stale data remains visible but is clearly labelled with an age such as `2m ago`.

Do not rely on red/green alone to communicate state. Pair color with text, iconography, or position and keep daylight contrast high.

## SquareLine Workflow

1. Edit the visual layout and semantic object names in `Arduino/ui_project/SLS_1_28v2-20250807/`.
2. Export LVGL 9 C sources from SquareLine.
3. Deliberately synchronize the generated export in `Arduino/libraries/UI/`.
4. Update the handwritten UI controller to use generated object names such as `ui_BatterySocLabel` and `ui_ConnectionStatusLabel`.
5. Build with PlatformIO and test on the device.

Keep generated identifiers semantic and stable. Application behavior belongs in the UI controller, not in generated SquareLine event handlers. Preserve the current asset files in the export when they are reused; do not replace them with generic LVGL widgets merely to simplify the controller code.

## Delivery Sequence

1. Add `.gitignore`, `config-example.json`, `data/config.json` handling, `partitions.csv`, and bounded SPIFFS configuration loading before network code.
2. Add bounded Wi-Fi/MQTT reconnect behavior using validated SPIFFS configuration and verify that LVGL remains responsive during disconnection.
3. Implement catalog-driven telemetry snapshot parsing with serial fixtures: valid, missing field, malformed JSON, and oversized payload.
4. Replace the demo controls with a telemetry-value carousel, preserve the brightness item, and bind a simulated snapshot.
5. Add selected-value detail and connection-status views; unify encoder and touch navigation around the carousel index.
6. Connect the dedicated network task to live MQTT and validate against the configured `rv-control` Renogy topic.
7. Remove remaining demo-only value handlers and decorative long-running LED behavior unless consciously retained as a quiet status indicator.

Each stage must build independently. Keep SquareLine re-exports separate from networking and navigation changes so review remains clear.

## Renogy Browse Definition of Done

- PlatformIO builds with the existing ESP32-S3 and PSRAM configuration.
- Local credentials are present only in ignored SPIFFS `data/config.json`, never firmware source.
- The device enters a clear configuration-error state when SPIFFS cannot mount or `/config.json` is missing, oversized, malformed, or invalid.
- Valid Renogy messages update all available fields within one UI refresh cycle without an application-level polling request.
- Missing fields show `--` without affecting valid values.
- The rotary encoder moves reliably across every telemetry value, retained brightness control, and connection-status item.
- Only the selected brightness item changes the physical backlight; no telemetry item accepts a local edit or MQTT write.
- The Renogy carousel reuses the existing SquareLine visual assets and selected/unselected control treatment; new artwork is added only where needed for measurement clarity.
- The UI remains responsive through Wi-Fi loss, broker restart, malformed messages, oversized messages, and reconnect attempts in the separate network task.
- Status distinguishes fresh, stale, unavailable, invalid-payload, and connection-error conditions.
- Serial output reports configuration, Wi-Fi, MQTT, subscription, telemetry freshness, and recoverable-error transitions without exposing secrets or flooding repeated failures.
- Encoder and touch can reach each initial screen without a navigation trap.
- No MQTT publish, `/set` subscription, or write-back path is added.
- SquareLine source and generated output are synchronized.