# Axis scales and peak markers on the spectrum plots, Complex I/Q Frequency Data Stream page

![The Complex I/Q Frequency Data Stream page on an original Pluto: magnitude and phase, real and imaginary, Bode plot (wrapped and unwrapped phase) and Smith chart, two tones 12 dB apart](complexiq-demo-pluto-views.gif)

*The Complex I/Q page on an original Pluto (two tones from a Pluto+ over a 40 dB cable, `complexiq-demo-pluto-views.gif`).*

## What changed

1. **Axis scales with units on the Spectrum, Radio Astronomy and Signal Classifier plots.**
   Frequency in MHz along the bottom (a value at every vertical gridline), level in dB at
   the left edge (a value at every horizontal gridline), axis titles `Frequency (MHz)` and
   `dB (uncal.)`. The labels are an overlay (`spDrawAxes()` in `plotaxes.jsx`) on the
   existing full-canvas graticule, so cursor, zoom and touch handling are unchanged.
   The Signal Classifier's *Reference match* plot also gets a frequency scale and a
   `normalised level (unitless)` scale from -1 to +1. The level is not dBm, there is no
   absolute calibration, so the axis says `dB (uncal.)`.
2. **I/Q Waterfall renamed to Complex I/Q Frequency Data Stream** (`complexiq.jsx`, route
   `complexiq`). It was never a waterfall, it shows one complex spectrum per frame.
3. **A switch between four views of the complex data `X[f] = I + jQ`**, all with labelled
   axes and units:

   | View | Plots |
   |---|---|
   | Magnitude / Phase | top: `20*log10 abs(X)` in dB re 1 LSB over frequency (MHz); bottom: phase `atan2(Q, I)` in rad from -pi to +pi over frequency, dots coloured by phase |
   | Real / Imaginary | top: real part, bottom: imaginary part, both over frequency (MHz), different colours. Scale selectable: unit circle with the peak rotated to 1 + j0 (default, fixed scale -1.1 to +1.1, does not fluctuate), unit circle (divided by the peak magnitude only, the peak phase still changes from frame to frame), or raw LSB (autoscale) |
   | Bode plot | top: dB over log frequency offset from the LO; bottom: phase in degrees over the same axis, wrapped to -180..+180 (default, the line is broken where the phase jumps across the wrap) or unwrapped inside each run of bins (switch `phase wrapped / phase unwrapped`); positive or negative offsets selectable |
   | Smith chart | `Gamma = X[f] / X[peak]` (each bin normalised to the strongest bin, so abs(Gamma) <= 1), constant-r circles and constant-x arcs with labels, Re/Im(Gamma) scales, points coloured by frequency with a colour bar in MHz |

   Magnitude/Phase, Real/Imaginary and Bode have a bin-number scale along the top edge (0 to 4096
   with 2048 = LO; for Bode the offset from the LO in bins) in addition to the frequency scale.
   A readout line under the plot gives the peak (excluding DC +-2 bins) and the bin under the
   cursor: frequency in MHz, dB, phase in rad and degrees, I, Q and bin number. `Hold`
   freezes the stream. Phase dots, the Bode phase and the Smith chart only use bins within a
   selectable distance (20 to 80 dB) of the peak, because the phase of a noise bin is random.
4. **Peak markers** on Spectrum, Radio Astronomy (header field `PEAK ON/OFF`), Signal
   Classifier (always on) and the Complex I/Q page: the two strongest bins (the second at least
   6 bins from the first, the DC/LO bins skipped) get numbered triangles on the trace and a
   readout `P1`/`P2` with frequency in MHz and level in dB (shared code `spFindPeaks`/
   `spDrawPeaks` in `pages1.jsx`; the complex page has its own markers with phase, I/Q or
   Gamma in the labels). A signal exactly at the LO is not marked.
5. **dB scale fix (behaviour change).** The `/waterfall` bins are power (two tones 12 dB
   apart in amplitude arrive 17.7 times apart), but Spectrum, Radio Astronomy and Classifier
   converted them with `20*log10`, so level differences were shown twice as large (24.6 dB
   for 12.4 dB measured). They now use `10*log10`. Default REF is 100 (was 130), the REF
   unit reads dB, and bins above REF are marked with red ticks and `OVER REF: peak <value>
   dB (raise REF)`. The Radio Astronomy auto scale outlier filter was adjusted to the new
   clamp (floor -100 dB instead of -200 dB). If the old scale was deliberate, this is the
   one change to drop.
6. **Radio Astronomy frequency switching (FSW) fix.** After each retune the first frame
   that arrives still shows the old LO, so a strong line limited the SIG - REF ratio. The
   two frames after every flip are now dropped (`fswSkipRef` in `radioastro.jsx`), see the
   FSW section.
7. **CP-OFDM detector card on the Signal Classifier page.** The `cpdetect` program on the
   board (`app/cpdetect`, tezuka_fw PR 498) can publish its result as `state/cpdetect/<key>`
   (`cpdetect -l -i 2 -M -s <rate>`). The page receives those keys and shows detected or
   not, FFT size, guard length, subcarrier spacing, symbol length, the fractional carrier
   offset, peak and ratio; it shows `not running` when nothing arrived for 10 s. Display
   only, no change on the board. Test: section 10.

The Complex I/Q page needs the `/iq-waterfall` channel of maia-httpd (maia-sdr PR #12).
Without it the page shows `disconnected` and no data. The raw capture card is unchanged.

## How it was tested

Rig: original Pluto (192.168.2.1) and Pluto+ (192.168.3.1) on a cabled 40 dB link at 2.4 GHz,
Chrome. The other board transmits a known signal and the page is compared with an
independent python decode of the same stream (`test/reference_peak.py`, uses the `hwlib.py`
of maia-sdr PR #12). Pages were served in two ways: from a local web server and pointed at the
boards (host-served), or by the boards themselves (board-served: first the `bundle.py` output
copied to `/root/index.html`, later the page inside the firmware image). Every Radio Astronomy CAL and
FSW result and everything in section 9 is board-served from firmware images. The check scripts of the final
suite (`rf_iq_check.py`, `rf_phase_check.py`, `rf_iq_frame_phase_check.py`) are in maia-sdr PR #12.

Test signals: one tone, or two tones of different level: 820.0 kHz and 840.4 kHz in a
seamless cyclic TX buffer (8.4 bins apart at 10 MS/s), the second at a quarter of the
amplitude, i.e. 12.04 dB lower (measured 12.0 to 12.5 dB).

### 1. Synthetic stream with exactly known content (checks the page's own maths)

`test/mock_iq_waterfall.py` serves frames on a local WebSocket (center 2400 MHz, span 10 MHz).
First-order low-pass `H(f) = 20000 / (1 + j f / 500 kHz)`, Bode view, cursor on the trace:

| offset | expected dB | shown dB | expected phase | shown phase |
|---|---|---|---|---|
| 100 kHz | 85.85 | 85.8 | -11.3 deg | -11.3 deg |
| 500 kHz | 83.01 | 83.0 | -45.0 deg | -45.0 deg |
| 2 MHz | 73.72 | 73.7 | -76.0 deg | -76.0 deg |

The Smith chart of the same stream is a circle of radius 0.5 centred on +0.5 with the
positive offsets on the lower half and the high-frequency ends at the centre
(`mock-rc-smith.jpg`, `mock-rc-bode.jpg`), as `H(f)/H(f_ref)` should be. Three single-bin
tones (`mock-tones-magphase.jpg`):

| bin | expected | shown |
|---|---|---|
| 2300 | 2400.615234 MHz, 86.02 dB, +0.500 rad | 2400.615234 MHz, 86.0 dB, +0.500 rad |
| 2600 | 2401.347656 MHz, 66.02 dB, -2.000 rad | 2401.347656 MHz, 66.0 dB, -2.001 rad |
| 1500 | 73.98 dB, +2.5 rad | dot at the right place in the phase plot (the cursor, 1 px = 3.4 bins, could not be placed exactly on this bin) |

### 2. Known signals over the cable, host-served page, both boards

Two equal tones, peak (excluding DC) in the browser against python decoding 13 or 38 frames:

| RX board | rate | python peak | browser peak |
|---|---|---|---|
| Pluto+ | 10 MS/s | bin 2364, 2400.771484 MHz, 63.43 dB | bin 2364, 2400.771484 MHz, 63.4 dB |
| Pluto | 10 MS/s | bin 2404, 2400.869141 MHz, 63.74 dB | bin 2404, 2400.869141 MHz, 63.7 dB |
| Pluto+ | 30.72 MS/s | bin 2386, 2402.535000 MHz, 62.73 dB | bin 2386, 2402.535000 MHz, 62.7 dB |
| Pluto | 30.72 MS/s | bin 2399, 2402.632500 MHz, 63.23 dB | bin 2399, 2402.632500 MHz, 63.2 dB |

The peak is -48.5 kHz from the transmitted tone, the carrier offset of the two references on
this rig (about -49 kHz). At 30.72 MS/s the two tones are within 0.5 dB, so the peak may be
either. The phase of a tone changes from frame to frame (the references of the two boards are
not locked), so it cannot be compared with a fixed value on real signals; the synthetic test
does that. Screenshots `pluto-2tone-10msps-{magphase,reim,bode,smith}.jpg`,
`plutoplus-2tone-10msps-{magphase,reim,bode,smith}.jpg`, `pluto-2tone-30p72msps-magphase.jpg`,
`plutoplus-2tone-30p72msps-magphase.jpg`, and the other pages
`{pluto,plutoplus}-{spectrum,radioastro,classifier}.jpg`: the tone sits at 2400.77 MHz on the
new scale and the labels follow the span (10 MHz: 2395 to 2405; Radio Astronomy 2.5 MHz:
2398.75 to 2401.25; 30.72 MHz: 2384.640 to 2415.360).

### 3. Page served by the boards, first image

The `bundle.py` output was copied to `/root/index.html` of both boards and opened from
`http://192.168.2.1/` and `http://192.168.3.1/`: python peak equals browser peak (Pluto+: bin
2372, 2400.791016 MHz, 63.3 dB; Pluto: bin 2404, 2400.869141 MHz, 63.8 dB against 63.78 dB).
All four views, the negative-offset Bode view, Spectrum, Radio Astronomy and Signal Classifier
draw on both boards without a JavaScript error (`boardserved-{pluto,plutoplus}-{spectrum,radioastro}.jpg`,
`boardserved-plutoplus-classifier.jpg`, `boardserved-{pluto,plutoplus}-complexiq-{magphase,realimag,bode,smith}.jpg`,
`boardserved-plutoplus-complexiq-bode-negative.jpg`). The
Signal Classifier on the Pluto matched a real interferer around 2402 MHz (CW / Beacon, 53 percent)
and drew the Reference match plot with data (`boardserved-pluto-classifier-real-match.jpg`, first
label layout). The labels of that plot were then moved up so they do not collide with the
`matched:` caption; the final layout was checked with a synthetic curve on the real canvas
(`classifier-reference-match-axes-synthetic.png`) and has not seen a real match.

### 4. Two tones with different levels

With equal levels the peak can be either tone. With the second tone 12.04 dB lower the peak is
always tone 1 and tone 2 is the 2nd peak (own readout line). Board-served, 10 MS/s:

| RX board | python (13 frames) | browser |
|---|---|---|
| Pluto+ | peak bin 2364, 2400.771484 MHz, 66.02 dB; 2nd bin 2372, 2400.791016 MHz, 53.82 dB | peak bin 2364, 66.0 dB; 2nd bin 2372, 2400.791016 MHz, 53.8 dB, 12.2 dB below |
| Pluto | peak bin 2404, 2400.869141 MHz, 66.36 dB; 2nd bin 2412, 2400.888672 MHz, 54.45 dB | peak bin 2404, 66.4 dB; 2nd bin 2412, 2400.888672 MHz, 54.4 dB, 12.1 dB below |

Screenshots `boardserved-{pluto,plutoplus}-uneven-tones-magphase.jpg`.

### 5. Normalised real and imaginary part, bin scale

Synthetic stream (`mock_iq_waterfall.py tones`): the strongest tone plots at exactly Re 1.0,
Im 0.0 with the default scale, and the tone 12.0 dB below (2.0 rad ahead of the peak) reads
Re -0.104, Im +0.227 at the cursor, as 0.25 cos 2 and 0.25 sin 2 predict. On the Pluto+ with
the two real tones the peak is at 1.0 and its phase no longer moves between frames
(`boardserved-plutoplus-reim-unitcircle.jpg`, `boardserved-plutoplus-binscale-{magphase,bode}.jpg`).

### 6. Peak markers on the complex page

Board check on the Pluto+ (unequal tones, 10 MS/s): marker 1 at 2400.769043 MHz, 65.8 to
65.9 dB, marker 2 at 2400.791016 MHz, 53.7 to 53.8 dB, the same values as the readout line; in the
Smith chart marker 2 reads Gamma = 0.06 + j0.25, magnitude 0.26 (the 11.9 to 12.1 dB difference).
Labels of the two markers can overlap when the peaks are only a few bins apart
(`boardserved-plutoplus-markers-{magphase,reim,bode,smith}.jpg`).

### 7. Spectrum, Radio Astronomy and Classifier: dB scale and peak markers

Pluto sends two tones of different amplitude at 10 MS/s, Pluto+ receives (Spectrum page, host-served):

| | Python decode of /waterfall | Page | Complex I/Q stream |
|---|---|---|---|
| P1 | bin 2364, 2400.771484 MHz, 10*log10(2.21e9) = 93.4 dB | 2400.771861 MHz, 93.4 dB | 66.2 dB |
| P2 | bin 2372, 2400.791016 MHz, 10*log10(1.25e8) = 81.0 dB | 2400.791402 MHz, 81.1 dB | 53.8 dB |
| P1 - P2 | 12.4 dB | 12.3 dB (was 24.6 dB with 20*log10) | 12.4 dB |

The page frequency is about 0.4 kHz above the decode (the page drops the step-index value
`f[0]` and maps bin i to i/(n-1)); below one bin (2.4 kHz), and cursor and axes share the mapping.
Board-served on the Pluto: P1 2400.869565 MHz 93.3 dB, P2 2400.891549 MHz 81.3 dB (decode
2400.869141 and 2400.888672 MHz). Radio Astronomy and Classifier read the same peak with the same
signal. Bins above REF are marked (`boardserved-plutoplus-spectrum-over-ref-marker.jpg`): a strong
signal no longer looks capped. Screenshots `spectrum-peak-markers-pluto-plus.jpg`,
`radioastro-peak-markers-pluto-plus.jpg`, `classifier-peak-markers-pluto-plus.jpg`.
On the Radio Astronomy page the second marker is sometimes a spur of the transmitting Pluto
(2400.193 MHz, 92 dB) instead of the second tone: it appears when the Pluto transmits and the
Pluto+ receives and is absent the other way round; the marker reports the second highest bin of
what is displayed (the cause of the spur was not verified).

### 8. Radio Astronomy CAL and FSW

**CAL.** The transmitter was silenced (TX gain -89 dB; killing the writer is not enough, the
cyclic buffer keeps repeating), CAL pressed with 12 to 15 frames in the window (ratio around
0 dB), then the tones were switched on and AUTO pressed. Pluto+ page: P1 2400.771251 MHz
+78.3 dB, P2 2400.792013 MHz +66.2 dB (12.1 dB apart); Pluto page: about +66 dB and +55 dB.
The markers follow the largest ratio bins and the AUTO scale fits them
(`boardserved-{plutoplus,pluto}-radioastro-cal.jpg`).

**FSW.** The receive LO alternates between CENTER and CENTER + OFS every INTEG seconds
(2400.000 and 2400.500 MHz, read over ssh) and the page shows SIG - REF.

*Problem.* With a strong tone SIG - REF showed only -16 to +8 dB at INTEG 5 s, where two clean dwells
give about +-70 dB (`boardserved-plutoplus-radioastro-fsw.jpg`); INTEG 1 s was unusable.
*Cause (measured):* an MQTT retune command reaches the LO within 130 to 220 ms (six retunes timed on
the Pluto+), so the retune is not the problem. The /waterfall frames are: after a retune the first
frame that arrives still shows the old LO (at a 0.42 s frame period the first frame after the command
was stale in 4 of 6 retunes, every later frame was new; `test/lag.py`). At the page's 1 fps this is about
one stale frame per dwell, 1/INTEG of the window, and a stale fraction p of a tone 70 dB above the noise
limits the ratio to about 10*log10(1/p): 7 dB at INTEG 5 s, about 15 dB at INTEG 30 s (computed, not
measured at 30 s).
*Fix:* the frame that triggers the flip (taken before the retune) and the next one (still integrating
while the LO moves) are dropped; a dwell holds INTEG - 2 clean frames.

Results with the fix (images below, strong tones: Pluto TX gain 0; weak line: TX gain -64 dB, about 10 dB
above the noise; OFS 500 kHz):

| Case | Before the fix | With the fix |
|---|---|---|
| strong tones, INTEG 30 s | about 15 dB expected ceiling | Pluto+ page: P1 +74.2 dB, P2 +62.3 dB; Pluto page: +71.4 dB, +60.1 dB; mirror lines about -72 dB at 2400.27 MHz |
| strong tones, INTEG 5 s | about +7 dB | Pluto+ page: P1 +73.1 dB, P2 +61.5 dB |
| strong tones, INTEG 1 s | unusable | Pluto+ page: P1 +73.3 dB, P2 +61.6 dB (one clean frame per dwell) |
| weak line, INTEG 30 s | +16.2 dB | +17.6 dB, flat baseline |
| weak line, INTEG 1 s | not measured | +17.3 to +17.6 dB over 20 s, flat baseline within about 1.5 dB |

The weak line sits at 2400.7719 MHz, 39.3 dB in the plain spectrum
(`boardserved-plutoplus-radioastro-weakline-plain.jpg`); its mirror image 0.5 MHz lower is a negative
spike, as frequency switching should give. P2 (+6 to +10 dB, 2399.975 MHz) is a spur of the
transmitting Pluto. Screenshots `boardserved-plutoplus-radioastro-fsw-weakline.jpg` (before),
`radioastro-fsw-fixed-strong-integ5-plutoplus.jpg`, `radioastro-fsw-fixed-strong-integ1-plutoplus.jpg`,
`radioastro-fsw-fixed-strong-integ30-pluto.jpg`, `radioastro-fsw-fixed-weak-integ1-plutoplus.jpg`.

### 9. Complex I/Q views served by the boards, and the final firmware images

Independent python decode of the stream, then the page (images with the dashboard, pages served by the boards):

| Board | Decode | Page |
|---|---|---|
| Pluto+ (2.5 MS/s, page default) | bin 3311, 2400.770874 MHz, 65.1 dB; bin 3344, 2400.791016 MHz, 52.7 dB | same bins and frequencies, 65.0/65.1 dB and 52.7 dB, 12.3 to 12.4 dB below the peak |
| Pluto (10 MS/s) | bin 2404, 2400.869141 MHz, 66.6 dB; bin 2412, 2400.888672 MHz, 54.4 dB | same bins and frequencies, 66.5/66.6 dB and 54.5 dB, 12.1 dB below the peak |

* Real / Imaginary (unit circle): the peak reads 1.000 + j0.000 on both boards; the second tone reads
  0.164 + j0.176 (Pluto+) and 0.193 + j0.156 (Pluto); recomputed from the page's own magnitude and
  phase readout this is 0.164 + j0.175 and 0.193 + j0.156.
* Bode: the offset of the peak from the LO reads 770.9 kHz (Pluto+, bin 3311, 770.87 kHz expected) and
  869.1 kHz (Pluto, bin 2404, 869.14 kHz expected). The phase is wrapped to -180..+180 by default (Pluto+ page: peak -91.2
  deg, second tone -2.7 deg, `boardserved-plutoplus-complexiq-bode-wrapped.png`); the unwrapped option shows the same tones
  between about +200 and -500 deg (`boardserved-plutoplus-complexiq-bode-unwrapped.png`). The other Bode screenshots in this
  folder were taken before the wrapped option and show the unwrapped phase.
* Smith chart: the peak at Gamma = 1.00 + j0.00, the second tone at 0.18 + j0.16 (Pluto+) and
  -0.14 - j0.21 (Pluto), |Gamma| 0.24 and 0.25; recomputed from the readout phases: 0.184 + j0.155 and
  -0.137 - j0.207.

Final test on firmware images built from this branch rebased onto upstream main (Pluto from the `.frm`, Pluto+
from the SD image; both serve the page from the image, identical to a fresh `bundle.py` of the source, and
the bitstreams equal their packages). The full RF suite (I/Q check with automatic carrier offset, phase check and
frame phase check at 2.5, 10, 20, 30.72 and 61.44 MS/s, both boards, the other board transmits) was run on two builds
of the final state; the dashboard bundle is byte-identical in both (the second build only differs in
documentation). Both runs: 30 steps, 70 checks ok, 0 FAIL, every settings restore matched.

| | run 1 | run 2 | both |
|---|---|---|---|
| I/Q frame rotation, measured minus predicted (rad) | -0.0009 to +0.0023 | -0.0009 to +0.0023 | -0.0009 to +0.0023 |
| frame rotation scatter (rad), frames off by more than 0.3 rad | at most 0.0019, none | at most 0.0019, none | at most 0.0019, none |
| OFDM log correlation | 0.989 to 0.999 | 0.987 to 0.999 | 0.987 to 0.999 |
| phase-derived minus time-domain frequency (Hz) | -10.2 to +2.4 | -12.2 to +2.9 | -12.2 to +2.9 |
| phase model error (deg / dB) | 0.09 to 1.02 / at most 0.30 | 0.06 to 0.99 / at most 0.30 | 0.06 to 1.02 / at most 0.30 |
| I/Q against raw phase difference (deg) | at most 11.2 | at most 13.9 | at most 13.9 |
| bins scored per tone / fractional bin position of the 30 tones | 6 or 7 / 0.08 to 1.0 | 6 or 7 / 0.0 to 0.92 | 6 or 7 / 0.0 to 1.0 |

* Spectrum page served by the boards: Pluto (run 1) P1 2400.869565 MHz 93.3 dB, P2 2400.891549 MHz 81.3 dB; Pluto+
  (run 2) P1 2400.769419 MHz 93.1 dB, P2 2400.791402 MHz 80.9 dB. FSW on the Pluto page: see above
  (`radioastro-fsw-fixed-strong-integ30-pluto.jpg`). The CAL and the Complex I/Q view results of this section were measured on pages served
  by the earlier newdash4 images, the FSW results on newdash5 and newdash6 images (the only dashboard code change after newdash4 is the
  FSW frame skip); in run 2 only the Spectrum page was spot-checked.

Two notes from the suite runs, both about the test scripts (maia-sdr PR #12), not the page: (a) with the tone
close to a half-bin position the per-frame I/Q peak can split over two neighbouring bins, which `rf_iq_check.py`
used to count as a failure (one run in 14 re-runs); the check now accepts a neighbouring bin. (b) one earlier run
printed a phase model error of 178.9 deg for a tone 0.51 of a bin from a bin centre; the cause was the frame
alignment of `rf_phase_check.py` near a half-bin tone (reproduced on synthetic captures, not on the boards), fixed
there; the final suite above uses the fixed script.

### 10. CP-OFDM detector card

An original Pluto receiving CP-OFDM (1024 subcarriers, guard 128, 2.5 MS/s, 2.4 GHz) from a Pluto+ over the
40 dB cable, `cpdetect -l -i 2 -M -s 2500000` running on the Pluto, the page opened on the Signal Classifier
(`boardserved-pluto-classifier-cpdetect.jpg`):

| Situation | Card |
|---|---|
| OFDM on air | detected: N 1024, G 128, spacing 2.441 kHz, symbol length 460.8 us, carrier offset 0.35 of the spacing (853 Hz, fractional part only), peak 0.993, ratio 5.03 |
| transmitter silent, `cpdetect` running | nothing found |
| `cpdetect` stopped | not running after 10 s |

This was first done with the page loaded from the Pluto's RAM (the bundle built from this branch copied over
`/root/index.html`), and then repeated in a flashed firmware image built with this branch's dashboard (original
Pluto and Pluto+, same page, the card on the Pluto's own page): detected with N 1024, G 128, spacing 2.441 kHz
and symbol length 460.8 us again. The firmware images of the other tests above were built before the card and
do not contain it. The result matches the `cpdetect` host and board tests (tezuka_fw PR 498).

### Reproduce

```
python3 test/mock_iq_waterfall.py rc 8765         # or: tones 8766
# serve the bundle, set MQTT_DEV_HOST to a board, then in the page console:
#   window._tezukaDevHost = '127.0.0.1:8765'      # and open the page again from the menu
python3 test/reference_peak.py TXHOST RXHOST 10000000 two 40   # or: tone, uneq (second tone 12 dB lower)
python3 test/lag.py RXHOST                         # stale frames after an MQTT retune (a tone must be received)
```

## Switching between the displays

Switching Spectrum, Radio Astronomy, Signal Classifier and the complex page back and forth
(checked on the Pluto+, 10 MS/s tone, reading `/api/spectrometer` and `/api/ad9361` after each switch): the
complex page, Spectrum and Signal Classifier change nothing on the board. **Radio Astronomy does**: opening it
sets the RX sample rate to 2.5 MS/s, the spectrometer to 1 frame per second and gain mode manual, and leaving
it does not restore them, so the Spectrum page afterwards shows a 2.5 MHz span at 1 fps (existing behaviour,
unchanged). Dragging or wheeling the Spectrum canvas sends hardware centre and span commands to the board.

On the complex page, magnitude bins that are exactly zero are drawn at -6 dB (the plot floor is
`20*log10(max(abs(X), 0.5))`), which shows as a flat floor in the noise.

## Limits and not tested

* Chrome only; no touch, no other browsers; high-DPI scaling only in the new page. Other boards were not tested
  (the complex page needs the I/Q channel of maia-sdr PR #12).
* The final Reference match label layout was only checked with a synthetic curve (section 3).
* The Bode phase is wrapped (or, selectable, unwrapped inside each run of bins above the threshold); for a single
  tone it only shows the window leakage. It is meaningful for a broadband stimulus (see the synthetic low-pass).
* The Smith chart normalises to the strongest bin. It is a display of the spectrum, not an S11 measurement.
* Level is uncalibrated (dB re 1 LSB on the complex page, 10*log10 of the spectrometer power elsewhere), not dBm.
  Other pages with plots (signal generator, Webtioune, QO-100 spectrum, charts on the overview) were not changed.
* In CAL or FSW mode the page shows a ratio in dB and the peak markers then mark the strongest ratio bins.
* FSW on the Pluto's own page was tested at INTEG 30 s (above) and, on the final firmware image, at INTEG 5 s and
  1 s: a strong tone read +67.8 dB and +68.3 dB above the reference band with a flat baseline (page gain 40 dB,
  Pluto+ transmitting; a different gain from the host-served runs, so the level is not comparable with their
  +73 dB). The stale frame count after a retune was measured at a 0.42 s frame period, not at the page's 1 fps.
