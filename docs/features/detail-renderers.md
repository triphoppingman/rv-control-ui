# Detail Renderers

## Purpose

Each `catalog.items[]` item selects a read-only detail presentation with the
optional `display_mode` field. Supported values are `dial`, `chart`, `bar`,
`threshold`, and `power_flow`. The default is `dial`, so existing catalogs
remain compatible.

All renderers run only on Arduino's loop thread through `UiController`. MQTT,
Wi-Fi, and encoder tasks never create or modify LVGL objects.

## Common Fields

Every display mode uses the normal catalog fields:

- `title`, `carousel_title`, `source`, `palette`, and `value_key`
- `unit`, `icon`, and `screen`
- `arc_min` and `arc_max` as a fixed visual range
- `precision`, `compact`, and `font_size` for the current value

The item receives `display_background` and `tick_label_color` from its named
palette. `display_background` is applied to the generated detail screen.

Palettes may also set an optional `background_image` to select a compiled
SquareLine detail-screen asset:

```json
{
  "id": "temperature",
  "tick_label_color": "#F5F7FA",
  "display_background": "#493038",
  "background_image": "temperature"
}
```

Valid values are `default`, `electrical`, `temperature`, `light`, and `none`.
The field defaults to `default` when it is absent, preserving compatibility
with existing `config.json` files. For dial rendering, `default` selects the
existing electrical artwork on electrical screens and the existing temperature
artwork on temperature screens. `none` deliberately removes the image and
uses only `display_background`. The selector accepts only compiled SquareLine
assets; it never loads image paths from SPIFFS.

```json
{
  "title": "Battery voltage",
  "carousel_title": "Battery\nVoltage",
  "source": "battery_monitor",
  "palette": "battery",
  "value_key": "battery_voltage",
  "unit": "V",
  "icon": "battery",
  "screen": "electrical",
  "display_mode": "chart",
  "arc_min": 10,
  "arc_max": 16,
  "precision": 1,
  "font_size": 40
}
```

## Dial

```json
"display_mode": "dial"
```

`dial` is the default mode. It preserves the existing SquareLine electrical
and temperature dial presentations. Use it when a scalar value's immediate
position within a well-known range is more useful than its recent trend.

## Chart

```json
"display_mode": "chart"
```

`chart` shows the same formatted current value used by dial mode, along with a
fixed-range line chart, grid divisions, min/mid/max labels, and collection
status. Use it for slowly changing values such as battery voltage, state of
charge, or temperature.

History is strictly volatile:

- Each accepted MQTT snapshot is appended once on the UI loop thread.
- Up to 60 samples are retained per configured telemetry item.
- Samples live in RAM only; no telemetry is written to SPIFFS, NVS, or flash.
- History resets after restart, reset, or power loss.
- Missing values render as gaps and are never interpolated.

`arc_min` and `arc_max` define a fixed Y-axis range. Keep the range realistic:
values outside it are clipped to the visible chart boundary, which can hide
variation at either extreme.

## Bar

```json
"display_mode": "bar"
```

`bar` renders the same current formatted value above a horizontal fill bar.
`arc_min` and `arc_max` define the fixed endpoints. It is suited to
percentages, tank levels, and other bounded quantities where fill level is easy
to scan.

Incoming values outside the configured range are clamped visually to the
nearest endpoint. The incoming telemetry is not modified.

## Threshold

```json
{
  "display_mode": "threshold",
  "arc_min": 10,
  "arc_max": 16,
  "threshold_low": 11.8,
  "threshold_high": 14.8
}
```

`threshold` renders a current value, fill bar, and a state label. The normal
range is inclusive: a value at either configured threshold is shown as normal.
Values below `threshold_low` or above `threshold_high` are shown as a warning
state; unavailable values show `No data`.

`threshold_low` defaults to `arc_min` and `threshold_high` defaults to
`arc_max`. The validator requires:

$$
arc\_min \le threshold\_low \le threshold\_high \le arc\_max
$$

Thresholds are display context only. The firmware remains read-only: they do
not create alarms, publish MQTT messages, or control RV equipment.

## Power Flow

```json
{
  "title": "Solar power flow",
  "carousel_title": "Solar\nFlow",
  "source": "solar_controller",
  "palette": "solar",
  "value_key": "pv_power",
  "unit": "W",
  "icon": "solar",
  "screen": "electrical",
  "display_mode": "power_flow",
  "flow_source_key": "pv_power",
  "flow_battery_key": "battery_power",
  "flow_load_key": "load_power",
  "arc_min": -2000,
  "arc_max": 2000
}
```

`power_flow` renders source, battery, and load values on the electrical detail
screen. `flow_source_key`, `flow_battery_key`, and `flow_load_key` are required
and must name `value_key` values declared by catalog items. This ensures the
network snapshot contains every value the renderer needs.

Use compatible units and a documented sign convention, normally watts. The
current implementation presents a logical source-to-battery-to-load path. It
does not infer flow direction or calculate derived power from voltage/current.
A missing component renders as `--`.

## Renderer Factory and Lifecycle

`UiController` owns one `DetailRendererFactory`, which preallocates the dial
renderer and one chart, bar, threshold, and power-flow renderer slot for each
catalog telemetry item. The factory performs no heap allocation and is not a
singleton.

When a telemetry detail opens, `UiController` selects a renderer through the
validated `display_mode`, hides the prior active renderer, and refreshes only
the selected renderer. Renderer objects retain their LVGL references, but only
one is visible on the shared generated detail screen.

## Operational Notes

- Use `chart`, `bar`, `threshold`, and `power_flow` with `"screen": "electrical"`.
  Temperature entries should remain `dial` until a dedicated temperature layout
  is implemented for the other modes.
- Keep ranges fixed and meaningful. Auto-scaling is intentionally not used.
- The chart line and renderer status colors are currently fixed for readability;
  palette colors currently control the background and dial tick labels.
- The parser rejects unknown display modes, invalid ranges, invalid thresholds,
  missing power-flow keys, and power-flow keys that are not catalog `value_key`
  values.
- Upload `data/config.json` with `~/.platformio/penv/bin/pio run --target uploadfs`
  after changing catalog configuration. Changes take effect after restart.

## TODO: Future Renderer Modes

- `StatusDetailRenderer`: Present discrete inverter, charger, transfer-switch,
  connectivity, or fault states as a large state label, matching icon/color,
  and last-update age. This requires a typed string or enum telemetry model;
  the current `TelemetryValue` model accepts numeric fields only.
- `GaugeClusterRenderer`: Present one dominant value with related supporting
  values, such as battery state of charge with voltage, signed current, and
  power. Add a grouped catalog contract that explicitly references the required
  existing `value_key` fields, following the `power_flow` pattern.
- `DurationRenderer`: Present elapsed time such as snapshot age, charge time,
  generator runtime, or outage duration. Prefer existing snapshot or connection
  timestamps rather than creating periodic flash-backed state.
- `EventTimelineRenderer`: Present recent source-state transitions, faults,
  reconnects, or generator events. Add only when the collector publishes real
  transition events; do not infer events from ordinary numeric snapshots.
- `DistributionRenderer`: Present related contributions as a compact stacked
  bar. Defer this until the upstream contract defines a reliable total and
  compatible component values, so the UI does not imply a conservation rule
  the telemetry cannot guarantee.
