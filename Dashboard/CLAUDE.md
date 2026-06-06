# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A **no-build, browser-only** React dashboard for Tezuka SDR boards (Zynq-7020/AD9363). There is no npm, no bundler, no package.json. React 18 and Babel standalone are loaded from CDN in the HTML file; JSX is transpiled in the browser at runtime.

## Running the dashboard

Open `Tezuka Dashboard.html` directly in a browser (served from a local HTTP server or opened as `file://`). All `.jsx` files are loaded as `<script type="text/babel">` tags — they must be served, not opened via `file://` in Chrome (CORS restriction on script src).

```bash
# Any static server works:
python3 -m http.server 8080
# Then open http://localhost:8080/Tezuka%20Dashboard.html
```

`Tezuka Dashboard (standalone).html` is a self-contained single-file export (all JS inlined) for distribution. Edit source files, not the standalone.

## File map

| File | Role |
|------|------|
| `Tezuka Dashboard.html` | Entry point; loads all `.jsx` in order |
| `tweaks-panel.jsx` | `useTweaks` hook + draggable Tweaks panel (accent colors, density, sidebar labels) |
| `icons.jsx` | `<Icon name="..." size={N} />` SVG icon sprite |
| `charts.jsx` | Chart/gauge primitives: `Donut`, `BarGauge`, `RadialGauge`, `DialGauge`, `StreamChart`, `Sparkline`, `XYChart` |
| `paho-mqtt-min.js` | Paho MQTT client (copied from firmware MSD); loaded as a plain `<script>` before the JSX files |
| `data.jsx` | `useLiveData()` — real MQTT connection + 1 Hz simulation for unmapped fields; shared UI primitives: `Card`, `Pill`, `Toggle`, `Slider`, `Field`, `Select`, `TextInput`, `Checkbox` |
| `tuner.jsx` | `<FreqTuner>` — SDR++-style per-digit frequency/sample-rate input with mouse wheel, keyboard, and click |
| `pages1.jsx` | Dashboard overview + RF Parameters page |
| `pages2.jsx` | DATV Controller, Versions, Analysis, Network pages |
| `pages3.jsx` | Architecture page (clickable AD9361 block diagram) |
| `app.jsx` | App shell: `<Sidebar>`, `<Topbar>`, routing (hash-based), `<TweaksPanel>` wiring, operator profile |
| `styles.css` | All CSS — dark theme, CSS custom properties for accent/density |

## Architecture

**Global namespace via `window`**: because there's no module system, every file that exports symbols does so via `Object.assign(window, { ... })`. Load order in the HTML matters — `tweaks-panel.jsx` and `data.jsx` must come before any page file.

**Routing**: hash-based (`location.hash`). The `NAV` array in `app.jsx` defines all routes. Adding a page requires: (1) a new case in the `page()` switch, (2) an entry in `NAV`, (3) a title in `TITLES`.

**Live data**: `useLiveData()` in `data.jsx` connects to the MQTT broker at `ws://[hostname]:9001/mqtt` and subscribes to `state/#`. It also runs a 1 Hz simulation tick for system metrics (CPU, memory, temperature) that have no MQTT topic yet. Returns a `d` object; all pages that display live metrics receive `d` as a prop from `<App>`. `d.publish(path, value)` sends a `cmd/<path>` message to the broker.

**Theming**: CSS custom properties `--accent` and `--accent-2` are set on `:root` by `App` via the `useTweaks` hook. Density is set via `data-density` on the `.app` element.

**TweaksPanel protocol**: `useTweaks` / `TweakPanel` communicate with an outer host (e.g., an iframe host) via `postMessage` (`__activate_edit_mode`, `__deactivate_edit_mode`, `__edit_mode_set_keys`). In standalone use the panel is activated by the host; `TWEAK_DEFAULTS` is the `/*EDITMODE-BEGIN*/…/*EDITMODE-END*/` block that a host rewrites on disk.

## MQTT backend

The on-device backend is `api_controller.sh` (polling script, 2 s interval, `board/tezuka/common/overlay_tezuka/root/`). It uses Mosquitto; the browser connects via WebSocket (typically port 9001).

**Topic convention**:
- `state/<path>` — published by the device (read-only for the dashboard)
- `cmd/<path>` — subscribed by the device; publish here to change a setting

**State topics** (`state/…`):

| Topic | Description |
|-------|-------------|
| `state/rx/frequency` | RX LO frequency (Hz) |
| `state/tx/frequency` | TX LO frequency (Hz) |
| `state/rx/sampling` | RX sampling rate (Hz) |
| `state/tx/sampling` | TX sampling rate (Hz) |
| `state/rx/bandwidth` | RX RF bandwidth (Hz) |
| `state/tx/bandwidth` | TX RF bandwidth (Hz) |
| `state/rx/gain` | RX hardware gain (dB) |
| `state/tx/gain` | TX hardware gain (dB) |
| `state/rx/gain_mode` | RX gain control mode (`manual`, `slow_attack`, …) |
| `state/rx/ant` | RX RF port select |
| `state/tx/ant` | TX RF port select |
| `state/rx/active` | RX LO powerdown (0 = active) |
| `state/tx/active` | TX LO powerdown (0 = active) |
| `state/rx/rfinput` | RX RF input channel (1rx-1tx mode) |
| `state/rx/fir_enable` | RX FIR decimation filter enable |
| `state/rx/overload` | RX overload flag (0/1) |
| `state/rx/sweep/activate` | Sweep mode active (0/1) |
| `state/rx/sweep/frequency` | Sweep center frequency (Hz) |
| `state/rx/span` | Sweep span (Hz) |
| `state/main/serial` | Device serial number |
| `state/main/hw_model` | Hardware model string |
| `state/main/fw_version` | Firmware version string |
| `state/main/freq_correction` | XO correction (ppb) |
| `state/caps/<path>` | Capability ranges/options for the matching `state/<path>` |

**Command topics** (`cmd/…`):

| Topic | Effect |
|-------|--------|
| `cmd/rx/gain` | Set RX hardware gain (dB) |
| `cmd/rx/frequency` | Set RX LO frequency (Hz); respects sweep mode |
| `cmd/rx/span` | Set sweep span (Hz); >60 MHz enables sweep mode |
| `cmd/rx/rfinput` | Switch RX RF input |
| `cmd/rx/sweep/frequency` | Set sweep center frequency |
| `cmd/rx/sweep/span` | Set sweep span |
| `cmd/rx/sweep/activate` | Enable (1) / disable (0) sweep |

All payloads are plain text strings (numbers as decimal, flags as `0`/`1`). Messages are published with the retain flag (`-r`) so late-connecting clients get the last value immediately.

## Key conventions

- Each `.jsx` file declares its own `const { useState, useEffect, ... } = React` aliases (suffixed to avoid collisions, e.g. `useS1`, `useS2`) since there's no module scope.
- Charts are SVG-based with a `ResizeObserver` for responsive width.
- `FreqTuner` supports mouse wheel, up/down click on digit halves, and keyboard arrow/digit entry.
- The standalone HTML is machine-generated (bundler output). Edit source files only.
