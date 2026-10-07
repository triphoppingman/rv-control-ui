# Changelog

Notable changes to RV Control UI are recorded here.

## [Unreleased]

### Added

- Optional per-item `background_bands` for context-specific telemetry background colors, independent of display mode and low/high status thresholds.
- Added `input.return_on_single_click` to configure returning from a detail view with one click or the existing double-click gesture.
- Sized catalog items, telemetry snapshots, chart history, carousel entries, and mode-specific renderers from the configured display count instead of a fixed 16-item capacity.

### Changed

- Returning to the carousel now defaults to a single click.
