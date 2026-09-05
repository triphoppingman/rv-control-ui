# RV Control UI Guidelines

## Architecture

- Write application code under `src/`. Do not include or compile `Arduino/RotaryScreen_1_28/RotaryScreen_1_28.ino` from `src/main.cpp`; use it as the known-good template for hardware setup, encoder event queues, screen changes, and brightness behavior.
- Retain the existing SquareLine assets and visual language. SquareLine project files are in `Arduino/ui_project/`; PlatformIO uses the generated LVGL export in `Arduino/libraries/UI/`. Update generated output only through a deliberate SquareLine export.
- Keep `loop()` as the sole owner of LVGL objects. FreeRTOS input/network tasks may queue actions or copy data, but must never call LVGL.
- Run Wi-Fi and MQTT work in `MqttTelemetry`'s dedicated task. Treat MQTT input as untrusted: accept only the configured topic, bound payload size, parse JSON structurally, and retain the latest valid Renogy snapshot.

## Configuration and Safety

- Load Wi-Fi, MQTT, display, and logging settings from `/config.ini` on SPIFFS. The device-local `data/config.ini` is ignored by Git; update `config-example.ini` when the schema changes.
- Never hard-code, log, document, or commit Wi-Fi credentials, MQTT credentials, or real device identifiers.
- The firmware is read-only for RV telemetry. Do not add MQTT publishing, `/set` subscriptions, or hardware control paths without an explicit request.
- Preserve configurable display backlight sleep and wake it on touch or encoder activity without restarting the UI or network task.

## Code Style

- Use the existing tab indentation in `src/`.
- Add clear, purpose-oriented comments to variable declarations, classes, functions, methods, callbacks, and FreeRTOS task boundaries. Favor explanation of hardware, ownership, and intent over narration of syntax.
- Keep firmware code small and direct. Prefer fixed-size bounded buffers and typed models for configuration and telemetry data.
- Make incremental changes. Preserve known-good display, touch, encoder, DMA, and backlight setup unless a specific defect requires altering it.

## Validation

- Build with `~/.platformio/penv/bin/pio run`; the system `pio` command may be an incompatible PlatformIO 4 installation.
- Run `~/.platformio/penv/bin/pio run --target uploadfs` after changing `data/config.ini`.
- Do not claim hardware, Wi-Fi, MQTT, or serial validation passed unless it was actually performed.
- Diagnose device, display, input, Wi-Fi, and MQTT problems using the bring-up order and log-code reference in [UI_STEERING.md](../UI_STEERING.md#hardware-bring-up-and-troubleshooting) before changing driver or network code.
- Follow [UI_STEERING.md](../UI_STEERING.md) for the Renogy UI roadmap and acceptance criteria.