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

### PlatformIO Workflow

The standard `elecrow-crowpanel-128` PlatformIO environment remains the USB
firmware/filesystem path. `elecrow-crowpanel-128-ota` is a separate custom
upload environment that invokes `scripts/ota_upload.py`; its hook rejects
`uploadfs` and `buildfs`. The OTA hook is not active for USB environments.

PlatformIO's Python environment and ESP32 framework already provide all build
and firmware-update dependencies. The workstation uploader uses Python's
standard library; no additional Python package is required.

### Provisioning

Each device uses one random 256-bit shared key. Run
`scripts/ota_upload.py keygen` to create `secrets/ota.key` with mode 0600; after
creating and editing `data/config.json`, run
`scripts/ota_upload.py provision --config data/config.json` to add the matching
hexadecimal value to the device-local config. Neither command prints the key
or configuration. Both paths are ignored by Git. An empty `ota.shared_secret`
disables authenticated API access and OTA; the example configuration
deliberately leaves it empty.

Provision the key and install the OTA-capable firmware plus filesystem once
over USB. USB firmware and filesystem uploads do not read or require the key.
If the key is lost, provision a replacement through USB and reinstall the
filesystem; do not rotate only one side of the pair.

### Authenticated API

| Method | Path | Content type | Behavior |
| --- | --- | --- | --- |
| `GET` | `/api/ota/challenge` | JSON response | Returns a random, short-lived, one-use nonce and public OTA capabilities. |
| `POST` | `/api/firmware` | `application/octet-stream` | Streams one fixed-length application image to the inactive OTA slot. |

When a key is provisioned, `/api/config` GET/POST and `/api/restart` POST also
require HMAC authorization. Configuration GET does not return the OTA key.
Configuration POST preserves the provisioned key and cannot rotate or clear
it. `/api/info` remains unauthenticated and returns device/build metadata only.

The client signs a canonical message containing protocol version, method, path,
nonce, request size, SHA-256, and board target. The device consumes the nonce
once and compares HMAC-SHA-256 in constant time. The firmware endpoint verifies
the declared length, image type, target marker, and streamed SHA-256 before
activating the slot. Chunked transfer encoding, multipart forms, config images,
SPIFFS images, and arbitrary binaries are rejected.

Authentication does not encrypt HTTP traffic or authenticate responses. Use a
trusted LAN or the device hotspot only; do not expose the API to the Internet.
In particular, authenticated configuration GET responses still contain the
stored Wi-Fi and MQTT settings, so they must not be sent over an untrusted
network.

### Update Lifecycle

1. Check the device target and inactive-slot capacity; inspect the local ESP
  image and snapshot it to a temporary file before sending.
2. Obtain a nonce and authorize the exact image metadata with HMAC. Stream the
  fixed-length image once; never buffer firmware in device RAM or SPIFFS.
3. Abort on disconnect, timeout, oversize, wrong target, bad digest, or flash
  error. A failed upload never activates the partial image.
4. Activate only after exact byte count and SHA-256 validation. The client
  never retries a firmware POST after an ambiguous connection failure; it
  instead checks the device's running ELF identity and OTA state.
5. Defer Arduino's default eager confirmation. Roll back on critical display/UI
  startup failure, and confirm only after the UI loop has run successfully for
  the configured health interval. Wi-Fi and MQTT availability are not health
  requirements.

The receiver logs lifecycle events and byte counts only, never firmware data or
the shared key.

### API Errors

| Status | Meaning |
| --- | --- |
| `400` | Invalid length/headers, interrupted upload, wrong image target, or digest/update validation failure. |
| `401` | Missing, expired, replayed, or invalid HMAC authorization. |
| `403` | OTA disabled because no shared key is provisioned. |
| `409` | A restart is pending or the running image has not yet passed boot confirmation. |
| `413` | The image exceeds the inactive OTA partition capacity. |
| `429` | Challenge requests are rate limited. |
| `500` | The ESP32 update subsystem could not begin, write, or finalize the image. |

Error responses use the existing JSON form:

```json
{"error":"firmware image exceeds the OTA partition capacity"}
```

## Ownership and Implementation Boundaries

- `NetworkController` owns Wi-Fi, the HTTP server, status LED, and all API
  request processing in its network task.
- `ConfigApi` owns endpoint registration, bounded request validation, response
  serialization, authenticated maintenance actions, OTA state, and delayed
  restart scheduling.
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
- A valid application binary installs to the inactive OTA partition, restarts,
  and is confirmed only after healthy UI-loop operation.
- Invalid, unauthenticated, incomplete, wrong-target, or oversized uploads
  leave the currently booted firmware usable.
- A critical UI startup failure rolls back to the previous image; Wi-Fi/MQTT
  outages alone do not trigger rollback.
- USB firmware upload remains available with no OTA key and is unaffected by
  OTA authentication. OTA filesystem targets are rejected.
- Firmware upload does not invoke LVGL, publish MQTT messages, change RV
  hardware, or write configuration.
- Build with `~/.platformio/penv/bin/pio run`; perform OTA validation on a test
  device before treating the feature as production-ready.