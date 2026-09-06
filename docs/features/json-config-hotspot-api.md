# Feature: Unified JSON Configuration, Setup Hotspot, and Configuration REST API

Status: approved for implementation

## Summary

Replace the SPIFFS INI configuration and the separate display catalog with a single JSON configuration file, `/config.json`. When the device cannot join the configured Wi-Fi network — or no Wi-Fi network is configured — it starts a setup hotspot and exposes a minimal REST API for reading and replacing the running configuration. A validated POST writes `/config.json` and restarts the device so the new configuration is loaded cleanly.

This feature does not change the carousel, telemetry rendering, MQTT subscription behavior, or the read-only safety boundary. The display catalog moves into `config.json` with identical semantics.

## Motivation

- One file, one schema: `config.ini` and `display-catalog.json` are two formats, two parsers, and two upload steps for what is logically one device configuration.
- Field setup without a laptop: today, changing Wi-Fi credentials requires a USB cable, editing `data/config.ini`, and `pio run --target uploadfs`. A hotspot plus REST API lets a phone configure the device in place.
- Recoverable misconfiguration: a typo'd SSID or a moved network currently bricks the network path until a laptop is available. Falling back to a hotspot makes the device self-recoverable.

## Current State

- `src/config_loader.*` parses `/config.ini` (INI) into a typed `AppConfig`.
- `src/display_catalog.*` parses `/display-catalog.json` into a typed `DisplayCatalog`.
- `src/mqtt_telemetry.*` (`MqttTelemetry` class) runs Wi-Fi station mode and MQTT in a dedicated FreeRTOS task with bounded reconnects.
- `loop()` is the sole owner of LVGL; network and input tasks never touch LVGL.

## Target Design

### 0. Object-oriented structure and naming

The network task's class is renamed because its brief has outgrown MQTT: it now owns Wi-Fi station/AP modes, the MQTT client, the HTTP configuration API, and the NeoPixel status LED.

- `MqttTelemetry` → **`NetworkController`**, in **`src/network_controller.*`** (files renamed from `src/mqtt_telemetry.*`). This is the single owner of all networking: Wi-Fi station and hotspot modes, the MQTT subscriber, the HTTP server, and the status LED. It runs the dedicated FreeRTOS task, holds the connection state machine, and exposes `begin(...)` and `copyLatestSnapshot(...)` to the rest of the firmware. `loop()` still consumes snapshots; `NetworkController` still never touches LVGL.
- The API is a class, **`ConfigApi`** (`src/config_api.*`), owned and `handleClient()`-pumped by `NetworkController`. It encapsulates the `WebServer` instance, the GET/POST handlers, validation handoff, and the atomic-write-and-restart logic. Keeping it a separate class (rather than free functions inside the network task) isolates the HTTP surface so it can be read and tested on its own.
- The status LED is a small class, **`StatusLed`** (`src/status_led.*`), wrapping the `Adafruit_NeoPixel` strip behind a `show(State)` method over a `enum class State { Starting, BrokerDown, Connected, Hotspot }`. `NetworkController` calls `show(...)` on state transitions; the LED class owns GPIO 48, the color map, and brightness, so no color/GPIO detail leaks into network code.
- The unified config loader stays a cohesive module (`src/config_loader.*`) but exposes a class-style API: a `ConfigStore`/`AppConfigLoader` that owns SPIFFS mounting, the bounded document, validation, and the typed `AppConfig` + `DisplayCatalog` output, plus the shared "validate this JSON document" entry point reused by `ConfigApi` for POST bodies.

Each class gets a documented public interface and private implementation, per the repo's comment style. No class reaches across another's responsibility: LVGL stays in `loop()`, networking in `NetworkController`, HTTP in `ConfigApi`, GPIO in `StatusLed`, and config parsing/validation in the loader.

Compile-time constants stay **externalized in their own header**, [src/constants.h](../src/constants.h), as today. This feature adds new named constants there rather than scattering literals through the classes, so every value is defined once, named for its purpose, and easy to audit:

| Constant group | Values |
| --- | --- |
| Hotspot | base SSID prefix, hotspot password, static AP address `192.168.77.1` |
| Fallback timing | association-cycle length (30 s), failed cycles before hotspot (2) |
| HTTP API | server port, POST body size cap, pre-restart grace delay (~500 ms) |
| SPIFFS paths | `/config.json`, `/config.json.new` |
| Status LED | NeoPixel GPIO (48), pixel index (0), per-state colors (blue/orange/green/red), dim brightness duty |

No magic numbers or path strings appear inside the new classes; each references a named constant from `src/constants.h`.

### 1. Unified `/config.json`

`/config.ini` and `/display-catalog.json` are replaced by one SPIFFS file, `/config.json`, same data folder and same upload flow (`pio run --target uploadfs`). The INI parser is deleted; the JSON catalog parser is merged into a single config loader that validates the whole document and produces the same typed `AppConfig` and `DisplayCatalog` structs the rest of the firmware already consumes.

Proposed schema (top level):

```json
{
  "wifi":     { "ssid": "...", "password": "..." },
  "mqtt":     { "host": "...", "port": 1883, "username": "", "password": "", "base_topic": "rv" },
  "display":  { "temperature_unit": "F", "expected_poll_interval_seconds": 60,
                "sleep_after_seconds": 300, "show_brightness": true, "show_wifi": true },
  "logging":  { "serial_level": "INFO" },
  "catalog":  { "sources": [...], "palettes": [...], "items": [...] }
}
```

- The `catalog` object is the current `display-catalog.json` content, moved verbatim (`sources`, `palettes`, `items` keep their existing keys, limits, and validation rules: up to 8 sources, 8 palettes, 16 items; identifier, topic-suffix, color, arc-range, and palette/source reference checks unchanged).
- All existing INI keys map one-to-one into the sections above. `show_brightness`/`show_wifi` become booleans (the INI `0`/`1` restriction disappears with the format).
- The temporary INI migration acceptance of `renogy_topic`/`hughes_topic` is dropped; topic suffixes have lived exclusively in the catalog for some time.
- The file is bounded (a single maximum size covering both the settings and the catalog), parsed once at boot with ArduinoJson into a bounded document, fully validated, and rejected as a whole on any error — no partially applied configuration, mirroring today's behavior.
- Wi-Fi credentials stay optional-but-bounded: an empty or absent `wifi.ssid` is now *valid* and means "no station network configured; run the hotspot" (see below). `mqtt.host` and `mqtt.base_topic` remain required so the telemetry configuration is always complete; MQTT simply never connects while the hotspot is active.

Tracked vs ignored files keep the current pattern, renamed:

| Item | Version control | Purpose |
| --- | --- | --- |
| `data/config.json` | ignored | Device-specific configuration uploaded into the SPIFFS image. |
| `config-example.json` | tracked | Complete JSON schema with safe placeholder values. |
| `config-example.ini`, `display-catalog-example.json` | removed | Superseded by `config-example.json`. |
| `src/config_loader.*` | tracked | Mounts SPIFFS, parses and validates `/config.json`, exposes typed settings + catalog. |
| `src/display_catalog.*` | removed | Merged into the unified config loader. |
| `src/network_controller.*` | renamed from `mqtt_telemetry.*` | `NetworkController`: Wi-Fi station/hotspot modes, MQTT subscription, owns `ConfigApi` and `StatusLed`. |
| `src/config_api.*` | new | `ConfigApi`: the HTTP `WebServer` and GET/POST `/api/config` handlers. |
| `src/status_led.*` | new | `StatusLed`: NeoPixel status indicator and its color map. |
| `src/constants.h` | tracked | All compile-time constants, extended with hotspot, API, and status-LED values. |

### 2. Setup hotspot fallback

The network task (`NetworkController`) gains an explicit hotspot mode:

- Enter hotspot mode when either:
  - `wifi.ssid` is empty (no station network configured), or
  - station mode cannot associate: after two consecutive failed 30-second association cycles (about 60 seconds of failed attempts), fall back to the hotspot. Station retries continue in the background while the hotspot is active.
- Hotspot parameters:
  - SSID: `rv-control-ui-XXXX` where `XXXX` is the last four hexadecimal digits of the interface MAC address (uppercase, from `ESP.getEfuseMac()`), making co-located devices distinguishable.
  - Password: `Password123!` (WPA2 minimum length satisfied).
  - Address: static `192.168.77.1/24` (`WiFi.softAPConfig()` before `WiFi.softAP()`).
- While in hotspot mode:
  - MQTT is not started (there is no routable path to the broker and no reason to burn reconnect attempts).
  - The REST API (below) runs on the hotspot interface.
  - The station connection keeps retrying in the background only when a station SSID is configured; a successful association exits hotspot mode, stops the AP, and resumes normal MQTT operation.
- The WiFi Info carousel screen surfaces hotspot state (SSID, `192.168.77.1`) instead of the normal station SSID/IP, so the mode is discoverable from the device itself. This is a display-only change to that existing screen's data source; no carousel structure change.
- Serial logs report hotspot entry/exit and the AP SSID; the AP password is a fixed, documented value (not a credential) and may appear in logs.
- The NeoPixel status LED (below) shows red while hotspot mode is active, giving an immediate at-a-glance indication without opening the UI.

### 3. NeoPixel status LED

The board carries a 5-pixel Adafruit NeoPixel (WS2812) strip on GPIO 48, and the `Adafruit_NeoPixel` library is already bundled under `Arduino/libraries`, so no new dependency or wiring is introduced. One pixel is used as a network status indicator:

| Color | State |
| --- | --- |
| Blue | Starting up / attempting station connection (boot, Wi-Fi association in progress; MQTT not yet attempted). |
| Orange | Wi-Fi station connected but the MQTT broker is not (connecting, reconnecting, or failing auth). |
| Green | Fully connected (Wi-Fi station associated **and** MQTT broker connected). |
| Red | Hotspot mode active (no station network configured, or station fallback entered). |

Design decisions:

- **Owner**: the NeoPixel is driven exclusively from the network task (`NetworkController`), which already tracks the exact Wi-Fi/MQTT connection state that determines the color. It never touches LVGL, so the `loop()`-owns-LVGL rule is unaffected — the strip is plain GPIO, not a display object.
- **Precedence**: hotspot (red) outranks everything. Otherwise the station state selects among the three remaining colors: no Wi-Fi → blue, Wi-Fi up but MQTT down → orange, both up → green. This cleanly distinguishes a network problem (blue) from a broker problem (orange), which is the most useful field-diagnostic split.
- **Brightness**: the pixel is run dim (a small fixed duty such as 32/255) so it is legible in daylight but not a distraction in a dark RV at night; it is a status indicator, not illumination.
- **Single pixel**: only pixel 0 is used; the other four pixels are cleared once at startup and left off. Using the strip as a progress/chase animation (as the reference sketch does) is deliberately not carried over — a static color communicates state more honestly than motion.
- **Update points**: color is set on state transitions only (boot, association success/failure, MQTT connect/disconnect, hotspot entry/exit), not polled in the loop, so there is no repeated GPIO churn.
- The existing backlight sleep/wake behavior is independent; the status LED does not sleep with the display backlight because its whole purpose is to be visible when the panel is dark.

### 4. Configuration REST API

A minimal HTTP server runs in the network task (which already owns all networking; `loop()`/LVGL is untouched). Proposal: the Arduino core's built-in `WebServer` (HTTP/1.1, synchronous, bounded) rather than adding a new library dependency. **The API is always exposed: it starts with the network task and stays up in both hotspot mode (on `192.168.77.1`) and normal station mode (on the device's LAN address).** This lets clients adjust display characteristics and any other configuration without reprogramming via PlatformIO. The API is bound to the device, read-only for telemetry, and the only mutation path restarts the device.

Endpoints (JSON bodies, `Content-Type: application/json`):

| Method | Path | Behavior |
| --- | --- | --- |
| `GET` | `/api/config` | Return the current running `/config.json` **in full, including the Wi-Fi and MQTT passwords**. Nothing is redacted or hidden.
| `POST` | `/api/config` | Validate the submitted JSON against the full schema (same validation path as boot-time load, including catalog rules and bounded sizes). On success: write atomically to SPIFFS (write to `/config.json.new`, `SPIFFS.rename()` over `/config.json`), respond `200`, wait ~500 ms so the HTTP response flushes to the client, then restart the device. On failure: respond `400` with a concise error message; the on-flash configuration is untouched. |
| `GET` | `/api/info` | Return a debugging snapshot: build date/time, chip model/revision, MAC, uptime, free heap, sketch size, and live network state (hotspot active, Wi-Fi/MQTT connected, SSID, IP, RSSI). |
| `POST` | `/api/restart` | Acknowledge with `200`, then restart after the ~500 ms grace delay. No body required. |

Details:

- **Validation before write**: the POST body is size-bounded (same cap as the boot loader), parsed with ArduinoJson, and must pass the identical validator used at startup. Rejecting malformed input with `400` means a bad POST can never wedge the boot path.
- **Atomic write**: write-then-rename avoids a truncated `/config.json` if power drops mid-write. SPIFFS rename-over-existing is supported by the IDF SPIFFS layer used here.
- **No redaction**: `GET` returns the exact on-flash configuration, secrets included, and `POST` persists exactly what it receives. There is no redaction marker, keep-existing sentinel, or merge behavior — the body is the whole new configuration.
- **Restart on success**: after acknowledging the response and a ~500 ms grace delay (so the client reliably receives the `200`), the device calls `ESP.restart()`. Boot then reloads `/config.json` through the normal path. There is deliberately no separate `POST /api/restart`: one round trip writes and applies the configuration, which guarantees the running configuration and the on-flash file can never diverge. If the new configuration has an unreachable Wi-Fi network, the hotspot fallback recovers the device (this is the safety property that makes remote reconfiguration acceptable).
- **No telemetry mutation**: the API exposes configuration only. It does not add MQTT publishing, `/set` subscriptions, or hardware control; the read-only boundary is unchanged.
- **Bounded server work**: requests are handled one at a time in the network task's loop; body reads are capped and there are no heap-allocated response buffers beyond the bounded config document.
- **No authentication, secrets in the clear**: the API has no authentication and `GET` returns Wi-Fi and MQTT passwords in plaintext to any client that can reach the device (the hotspot password, or simple LAN presence in station mode). This is a deliberate decision matching the local-only trust model already assumed for the unauthenticated MQTT broker, and it is documented here rather than implied. Anyone on the private RV network can read and reconfigure the device; do not expose it to an untrusted network.

### 5. What does not change

- Carousel structure, navigation, brightness control, backlight sleep/wake, and detail-screen rendering are untouched.
- Catalog semantics are unchanged; only the file that carries them changes.
- MQTT remains subscribe-only; the telemetry snapshot pipeline is untouched.
- The `rv-control` service contract is unchanged.
- The NeoPixel strip is used only as the static status indicator above; the reference sketch's chase animation is not carried over.
- mDNS advertisement (e.g. `rv-control-ui.local`) is **not** included in this pass, to keep the HTTP server surface minimal. Clients address the device by IP (`192.168.77.1` on the hotspot, or its LAN address in station mode). mDNS is an easy follow-up if name-based access is wanted later.

## Configuration and Safety Checklist (repo rules)

- [ ] No credentials in source, docs, logs, or committed files; `config-example.json` uses placeholders; `data/config.json` stays gitignored. (The API `GET` intentionally returns stored secrets at runtime; that is a documented trust decision, not a leak into source, docs, or logs.)
- [ ] Firmware stays read-only for RV telemetry: no MQTT publish path, no `/set` subscriptions.
- [ ] `loop()` remains the sole LVGL owner; the HTTP server, hotspot logic, and NeoPixel status LED live entirely in the network task and touch no LVGL objects.
- [ ] Bounded buffers and typed models: fixed-size config structs, size-capped file and POST bodies.
- [ ] Compile-time constants live only in `src/constants.h`; the new hotspot/API/LED values are named constants there, not literals in the classes.
- [ ] Backlight sleep/wake behavior preserved; the status LED is independent of panel backlight sleep.
- [ ] Tab indentation and purpose-oriented comments per the existing style.
- [ ] `config-example.json` updated whenever the schema changes.

## Serial Logging

Additions within the existing `[logging] serial_level` rules:

- Hotspot entry (with AP SSID and `192.168.77.1`), hotspot exit on station association.
- API start (once, at network-task startup, before any Wi-Fi mode is known) and bound address per mode; each POST result (`200` written-and-restarting or `400` reason) without logging payload contents.
- Network status-LED state transitions are logged at `DEBUG` alongside the existing connection-state logs they mirror (they add no new log events of their own).
- Rate-limited repeats for repeated failed station attempts, per the existing logging policy.

## Error and Edge Cases

- **Missing/empty/oversized/malformed `/config.json`** at boot: keep the current behavior of a clearly logged configuration-error state; additionally start the hotspot + API so the device can be repaired without USB. (Display still comes up; the carousel simply has no catalog items when the catalog section is invalid.)
- **Configured Wi-Fi never reachable**: hotspot fallback after the bounded retry cycles; API stays available; station retries continue.
- **POST while in station mode**: allowed; same validate-write-restart path.
- **Power loss during write**: rename-based atomic update; worst case the previous config remains.
- **New config points at a bad network**: hotspot fallback recovers the device; this is the intended self-healing loop.
- **SPIFFS full on write**: check the write result; on failure respond `500` and keep the old file (delete the `.new` file).

## Documentation Updates

- `README.md`: configuration section rewritten for `config.json`; new "Setup Hotspot", "Status LED", and "Configuration API" sections with `curl` examples.
- `UI_STEERING.md`: update the "Configuration Is External" section and boundary table to the JSON file and the new API; document the hotspot recovery path and the status-LED color legend in bring-up/troubleshooting.
- `data/README.md`: rename the config references.
- `.gitignore`: `data/config.ini` → `data/config.json`; remove stale catalog entry handling if present.
- Delete `config-example.ini` and `display-catalog-example.json`; add `config-example.json`.

## Implementation Plan

1. Rewrite `src/config_loader.*` as the unified JSON loader (a class-style API producing `AppConfig` + `DisplayCatalog`); delete `src/display_catalog.*`; update `src/main.cpp` includes/call sites.
2. Rename `src/mqtt_telemetry.*` to `src/network_controller.*` (`MqttTelemetry` → `NetworkController`); add hotspot mode and AP/station state handling. Add `src/status_led.*` (`StatusLed`, GPIO 48 pixel 0) and drive it from the network-task connection-state transitions. Extend `src/constants.h` with the hotspot, API, and status-LED constants used by both.
3. Add `src/config_api.*` (`ConfigApi`, the `WebServer`-based GET/POST with full-secret GET, atomic write, and deferred restart), owned and pumped by `NetworkController`; add a shared "validate JSON document" entry point in the config loader so boot and POST use one code path.
4. Add `config-example.json`; update README/steering/data docs and `.gitignore`; remove INI/catalog example files.
5. Build with `~/.platformio/penv/bin/pio run` and fix any errors. Hardware, Wi-Fi, hotspot, API, and status-LED behavior are validated on-device separately; nothing will be claimed as verified unless actually run.
