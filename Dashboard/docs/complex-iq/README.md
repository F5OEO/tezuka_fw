# Axis scales for the spectrum plots and the Complex I/Q Frequency Data Stream page

## What changed

1. **Axis scales with units on the Spectrum, Radio Astronomy and Signal Classifier plots.**
   Frequency in MHz along the bottom (a value at every vertical gridline), level in dB at
   the left edge (a value at every horizontal gridline), axis titles `Frequency (MHz)` and
   `dB (uncal.)`. The labels are an overlay (`spDrawAxes()` in `plotaxes.jsx`) on the
   existing full-canvas graticule, so cursor, zoom and touch handling are unchanged.
   The Signal Classifier's *Reference match* plot also gets a frequency scale and a
   `normalised level (unitless)` scale from -1 to +1.
   The level is `20*log10` of the FFT bin amplitude: it is **not** dBm, there is no absolute
   calibration, so the axis says `dB (uncal.)` (the existing REF/RANGE header fields still
   say dBm; they were not touched).
2. **I/Q Waterfall renamed to Complex I/Q Frequency Data Stream** (`complexiq.jsx`, route
   `complexiq`). It was never a waterfall, it shows one complex spectrum per frame.
3. **A switch between four views of the complex data `X[f] = I + jQ`**, all with labelled
   axes and units:

   | View | Plots |
   |---|---|
   | Magnitude / Phase | top: `20*log10 abs(X)` in dB re 1 LSB over frequency (MHz); bottom: phase `atan2(Q, I)` in rad from -pi to +pi over frequency, dots coloured by phase |
   | Real / Imaginary | top: I in LSB, bottom: Q in LSB, both over frequency (MHz), different colours |
   | Bode plot | top: dB over log frequency offset from the LO; bottom: unwrapped phase in degrees over the same axis; positive or negative offsets selectable |
   | Smith chart | `Gamma = X[f] / X[peak]` (each bin normalised to the strongest bin, so abs(Gamma) <= 1), constant-r circles and constant-x arcs with labels, Re/Im(Gamma) scales, points coloured by frequency with a colour bar in MHz |

   A readout line under the plot gives the peak (excluding DC +-2 bins) and the bin under the
   cursor: frequency in MHz, dB, phase in rad and degrees, I, Q and bin number. `Hold`
   freezes the stream. Phase dots, the Bode phase and the Smith chart only use bins within a
   selectable distance (20 to 80 dB) of the peak, because the phase of a noise bin is random.

The page needs the `/iq-waterfall` channel of maia-httpd (maia-sdr PR #12). Without it the
page shows `disconnected` and no data. The raw capture card is unchanged.

## How it was tested

Two ways, both in Chrome: sections 1 and 2 below with the page served from a local web
server and pointed at the boards (MQTT and WebSocket to the board address), and section 3
with the `bundle.py` output copied to each board's `/root/index.html` and opened from the
board's own web server (`http://192.168.2.1/`, `http://192.168.3.1/`).

Rig: original Pluto (192.168.2.1) and Pluto+ (192.168.3.1) on a cabled 40 dB link, firmware
v0.3.23-32-g0c2165c (tezuka_fw v0.3.23 + open fixes + maia-sdr PR #12 + upstream main), RX LO
2.4 GHz. The other board transmits a known signal, the plot is compared with an independent
python decode of the same `/iq-waterfall` stream (`test/reference_peak.py`, uses the
`hwlib.py` of maia-sdr PR #12).

### 1. Synthetic stream with exactly known content (checks the page's own maths)

`test/mock_iq_waterfall.py` serves frames on a local WebSocket. The page was pointed at it
(center 2400 MHz, span 10 MHz).

First-order low-pass `H(f) = 20000 / (1 + j f / 500 kHz)`, Bode view, cursor on the trace:

| offset | expected dB | shown dB | expected phase | shown phase |
|---|---|---|---|---|
| 100 kHz | 85.85 | 85.8 | -11.3 deg | -11.3 deg |
| 500 kHz | 83.01 | 83.0 | -45.0 deg | -45.0 deg |
| 2 MHz | 73.72 | 73.7 | -76.0 deg | -76.0 deg |

The Smith chart of the same stream is a circle of radius 0.5 centred on +0.5 with the
positive offsets on the lower half and the high-frequency ends at the centre
(`mock-rc-smith.jpg`), as `H(f)/H(f_ref)` should be.

Three single-bin tones (`mock-tones-magphase.jpg`):

| bin | expected | shown |
|---|---|---|
| 2300 | 2400.615234 MHz, 86.02 dB, +0.500 rad | 2400.615234 MHz, 86.0 dB, +0.500 rad |
| 2600 | 2401.347656 MHz, 66.02 dB, -2.000 rad | 2401.347656 MHz, 66.0 dB, -2.001 rad |
| 1500 | 73.98 dB, +2.5 rad | dot at the right place in the phase plot; the cursor (1 px = 3.4 bins) could not be placed exactly on this bin, so no readout was taken |

### 2. Known signals over the cabled link, both boards

Two tones, 820.0 kHz and 840.4 kHz (8.4 bins apart at 10 MS/s) in a seamless cyclic TX
buffer. Peak (excluding DC) in the browser against python decoding 13 or 38 frames of the
same stream:

| RX board | rate | python peak | browser peak |
|---|---|---|---|
| Pluto+ | 10 MS/s | bin 2364, 2400.771484 MHz, 63.43 dB | bin 2364, 2400.771484 MHz, 63.4 dB |
| Pluto | 10 MS/s | bin 2404, 2400.869141 MHz, 63.74 dB | bin 2404, 2400.869141 MHz, 63.7 dB |
| Pluto+ | 30.72 MS/s | bin 2386, 2402.535000 MHz, 62.73 dB | bin 2386, 2402.535000 MHz, 62.7 dB |
| Pluto | 30.72 MS/s | bin 2399, 2402.632500 MHz, 63.23 dB | bin 2399, 2402.632500 MHz, 63.2 dB |

The offset of the peak from the transmitted tone (a single 820 kHz tone, Pluto to Pluto+) is
-48.5 kHz, the known carrier offset of the two references on this rig (about -49 kHz).
At 30.72 MS/s the two tones are within 0.5 dB, so the peak may be either of them.
The phase shown in the readout equals `atan2(Q, I)` of the I and Q shown in the same
readout in every sample taken. Phase of a tone changes from frame to frame (the references
of the two boards are not locked), so it cannot be compared with a fixed expected value on
real signals, that is what the synthetic test above is for.
The Bode view of the Pluto trace shows a 20 dB peak at +49 kHz; probably the transmitter's LO
leakage shifted by the carrier offset (not verified).

The Spectrum, Radio Astronomy and Signal Classifier pages were checked on both boards with
the same signals: the tone sits at 2400.77 MHz on the new scale (marker 2400.767 MHz when
the cursor is placed on it, one pixel is 8 kHz), the labels follow the span (10 MHz: 2395 to
2405; Radio Astronomy 2.5 MHz: 2398.75 to 2401.25; 30.72 MHz: 2384.640 to 2415.360).
Screenshots: `pluto-*.jpg`, `plutoplus-*.jpg` in this directory.

### 3. From the boards' own web servers

The final bundle (`python3 bundle.py`) was streamed over ssh to `/root/index.html` of both
boards (the root file system is in RAM, a reboot restores the original page) and the same
checks were repeated at 10 MS/s with the two tones, Pluto+ receiving from the Pluto and the
Pluto receiving from the Pluto+:

| Page served by | python peak | browser peak (page origin = board) |
|---|---|---|
| Pluto+ (192.168.3.1) | bin 2364 / 2372 (two equal tones, 63.30 and 63.27 dB), 2400.771484 / 2400.791016 MHz | bin 2372, 2400.791016 MHz, 63.3 dB |
| Pluto (192.168.2.1) | bin 2404, 2400.869141 MHz, 63.78 dB | bin 2404, 2400.869141 MHz, 63.8 dB |

On both boards all four views of the complex page, the negative-offset Bode view, Spectrum,
Radio Astronomy and Signal Classifier were opened in turn: every view draws (non-empty
canvas), no JavaScript error was raised. Screenshots `boardserved-*.jpg`.
During this run the Signal Classifier on the Pluto matched a real signal (an interferer
around 2402 MHz, label CW / Beacon, 53 percent) and drew the Reference match plot with data
(`boardserved-pluto-classifier-real-match.jpg`, taken with the first label layout). The
frequency labels of that plot were then moved up so they do not collide with the
`matched:` caption; the final layout was checked by drawing a synthetic curve on the real
canvas (`classifier-reference-match-axes-synthetic.png`), the final layout has not seen a
real match.

### 4. Two tones with different levels

With equal levels the peak can be either tone. The second tone was transmitted 12.04 dB
below the first (amplitudes 1 and 0.25), so the peak is always tone 1 (820.0 kHz) and tone 2
(840.4 kHz) is the 2nd peak, which the page now shows in its own readout line (strongest bin
at least 6 bins from the peak and from DC). Board-served, 10 MS/s:

| RX board | python (13 frames) | browser |
|---|---|---|
| Pluto+ | peak bin 2364, 2400.771484 MHz, 66.02 dB; 2nd bin 2372, 2400.791016 MHz, 53.82 dB | peak bin 2364, 66.0 dB; 2nd bin 2372, 2400.791016 MHz, 53.8 dB, 12.2 dB below |
| Pluto | peak bin 2404, 2400.869141 MHz, 66.36 dB; 2nd bin 2412, 2400.888672 MHz, 54.45 dB | peak bin 2404, 66.4 dB; 2nd bin 2412, 2400.888672 MHz, 54.4 dB, 12.1 dB below |

Screenshots `boardserved-*-uneven-tones-magphase.jpg`. The measured ratio is 12.2 and 12.1 dB
(python 12.2 and 11.9 dB) against 12.04 dB transmitted.

### Reproduce

```
python3 test/mock_iq_waterfall.py rc 8765         # or: tones 8766
# serve the bundle, set MQTT_DEV_HOST to a board, then in the page console:
#   window._tezukaDevHost = '127.0.0.1:8765'      # and open the page again from the menu
python3 test/reference_peak.py TXHOST RXHOST 10000000 two 40   # or: tone, uneq (second tone 12 dB lower)   # transmit and decode in python
```

## Limits and not tested

* Chrome only; no touch, no other browsers; high-DPI scaling only in the new page.
* The final Reference match label layout was only checked with a synthetic curve (see
  section 3).
* Board-served tests: 10 MS/s only; 2.5 and 30.72 MS/s were checked with the host-served page.
* The Bode phase is unwrapped inside each run of bins above the threshold; for a single tone
  it only shows the window leakage. It is meaningful for a broadband stimulus (see the
  synthetic low-pass).
* The Smith chart normalises to the strongest bin. It is a display of the spectrum, not an
  S11 measurement.
* Level is dB re 1 LSB, uncalibrated. Other pages with plots (signal generator, Webtioune,
  QO-100 spectrum, charts on the overview) were not changed.
* Opening the Radio Astronomy page already sets span, gain mode and spectrometer mode on
  the board (existing behaviour); the boards were rebooted to their defaults after the test.
