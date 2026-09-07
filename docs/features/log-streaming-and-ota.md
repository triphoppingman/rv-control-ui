# Live Log Streaming and OTA Firmware Update

## Purpose

Add two device-local maintenance capabilities to the configuration API:

1. Inspect the firmware's current operational log state without a serial cable.
2. Replace the running firmware image over the local network.

Neither capability changes RV telemetry behavior. MQTT remains read-only, and
all HTTP handling remains owned by `NetworkController`'s dedicated task.

## Live Log State

### Scope

The API exposes current in-memory diagnostic output only. It does not persist
logs to SPIFFS, SD storage, NVS, or any remote service, and it must not attempt
to retrieve prior boot logs. A restart or power loss clears the log state.

The implementation maintains a bounded RAM ring buffer populated at the same
time as normal serial output. It must preserve the existing serial log output
and must never store Wi-Fi or MQTT credentials, raw configuration bodies, or
untrusted MQTT payload contents.

### Endpoint

| Method | Path | Behavior |
| --- | --- | --- |
| `GET` | `/api/logs` | Returns the current bounded log snapshot as JSON. |

The response should include a monotonically increasing sequence number and an
ordered `entries` array. Each entry contains a severity, a boot-relative
timestamp in milliseconds, and a bounded message string.

```json
{
  "sequence": 184,
  "entries": [
    {"timestamp_ms": 1240, "level": "INFO", "message": "Wi-Fi connected; RSSI=-58dBm IP=192.168.8.72"},
    {"timestamp_ms": 1390, "level": "INFO", "message": "MQTT subscribed to 2 catalog source topics"}
  ]
}
```

The initial implementation is a polling snapshot, not a long-lived TCP stream,
Server-Sent Event connection, WebSocket, or historical log API. Clients can
poll with `since=<sequence>` to receive entries newer than the supplied
sequence. The response still carries the newest sequence so a client can resume
after an empty response or a buffer rollover.

### Resource and Concurrency Rules

- Use fixed-capacity entries and fixed-size message buffers; do not allocate per
  log message.
- Limit output to a small, documented maximum number of entries and bytes per
  response.
- Protect the ring buffer while it is written from the Arduino loop and the
  network FreeRTOS task, then copy a stable snapshot before JSON serialization.
- Do not call LVGL or access display objects from logging or API code.
- Keep API request logging itself concise so polling `/api/logs` does not flood
  the buffer.

## OTA Firmware Upload

### Endpoint

| Method | Path | Content type | Behavior |
| --- | --- | --- | --- |
| `POST` | `/api/firmware` | `multipart/form-data` | Streams one PlatformIO-built ESP32 application image to the inactive OTA partition. |

The request uses a single file field named `firmware`. The service accepts only
the application binary produced for the configured CrowPanel environment, such
as `.pio/build/elecrow-crowpanel-128/firmware.bin`. It must not accept a SPIFFS
image, configuration JSON, archive, or arbitrary file format.

Example client request:

```sh
curl --fail-with-body -X POST http://192.168.77.1/api/firmware \
  -F firmware=@.pio/build/elecrow-crowpanel-128/firmware.bin
```

### Update Lifecycle

1. Reject concurrent configuration updates or firmware uploads with `409`.
2. Start the ESP32 `Update` session using the inactive OTA application
   partition and its reported capacity.
3. Stream upload chunks directly from the HTTP upload callback to `Update`.
   Never buffer the complete image in RAM, SPIFFS, or a `String`.
4. Abort and return `400` or `413` for an empty, oversized, interrupted, or
   failed upload. Preserve the running firmware image on every failure.
5. Call `Update.end(true)` only when the full upload completed successfully.
6. Return `200` with `{"status":"ok","action":"restarting"}`, allow a
   short response-flush grace period, then restart into the new OTA partition.

The implementation records only upload lifecycle events and byte counts in the
live log buffer. It must never log firmware bytes.

### API Errors

| Status | Meaning |
| --- | --- |
| `400` | Missing firmware field, empty upload, malformed multipart request, or update validation failure. |
| `409` | A configuration write, restart, or firmware update is already pending. |
| `413` | The image exceeds the inactive OTA partition capacity. |
| `500` | The ESP32 update subsystem could not begin, write, or finalize the image. |

Error responses use the existing JSON form:

```json
{"error":"firmware image exceeds the OTA partition capacity"}
```

## Security Model

The current configuration API is intentionally unauthenticated under a
local-only trust model and exposes configuration secrets by design. OTA upload
has the same network exposure, but it can execute arbitrary firmware; therefore
it raises the impact of that decision.

Before enabling the endpoint, the device must remain reachable only from a
trusted LAN or its setup hotspot. Do not expose it through WAN port forwarding,
public Wi-Fi, an untrusted guest network, or an Internet-facing reverse proxy.
Any future authentication mechanism must protect `/api/config`, `/api/logs`,
`/api/restart`, and `/api/firmware` consistently rather than securing OTA in
isolation.

## Ownership and Implementation Boundaries

- `NetworkController` owns Wi-Fi, the HTTP server, status LED, and all API
  request processing in its network task.
- `ConfigApi` owns endpoint registration, bounded request validation, response
  serialization, OTA state, and delayed restart scheduling.
- A dedicated bounded `LogBuffer` owns RAM-only entries and provides safe
  snapshot copies. It must not depend on `ConfigApi`, `NetworkController`, or
  LVGL.
- `UiController` remains the sole owner of LVGL and must not be changed for this
  feature.
- Compile-time capacities, endpoint-independent size limits, and restart grace
  timing belong in `src/constants.h`.

## Acceptance Criteria

- `GET /api/logs` returns current-boot entries in chronological order without
  affecting serial output or using persistent storage.
- A client can poll with the latest sequence and receive only newer entries.
- Normal API polling, MQTT reconnects, and UI activity remain responsive while
  log snapshots are requested.
- A valid `firmware.bin` upload installs to the inactive OTA partition, returns
  success, and restarts into the new image.
- Invalid, incomplete, or oversized uploads return a JSON error and leave the
  currently booted firmware usable.
- Firmware upload does not invoke LVGL, publish MQTT messages, change RV
  hardware, or write configuration.
- Build with `~/.platformio/penv/bin/pio run`; perform OTA validation on a test
  device before treating the feature as production-ready.