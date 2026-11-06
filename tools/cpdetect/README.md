# cpdetect - blind cyclic-prefix (CP-OFDM) detection

Finds the FFT size N, guard length G, symbol timing and fractional carrier
offset of a CP-OFDM signal from time-domain IQ, without knowing the standard.
It complements the PSD template matching in `app/classifier`: that classifier
sees power only, while the cyclic prefix is a property of the complex samples
(a phase-coherent repetition) and is invisible in a power spectrum.

| Part | What |
|------|------|
| `tools/cpdetect/cpdetect.py` | Host reference (numpy): CLI on a file or a libiio capture, `--selftest` |
| `app/cpdetect/cpdetect.c` | Same algorithm and thresholds in C for the board, via libiio or a file |
| `package/cpdetect` | Buildroot package (`BR2_PACKAGE_CPDETECT`, not enabled in any defconfig) |
| `tools/cpdetect/link_test.py` | Two-board cabled check: OFDM and control signals over the air path |

## Method

For a hypothesis (N, G), `y[i] * conj(y[i+N])` has a stable phase inside every
cyclic prefix and averages to zero elsewhere. Cumulative sums of that product
and of the energy give, for every window of G samples,
`rho[d] = |sum y[i] conj(y[i+N])| / energy`. Folding `rho` modulo `P = N + G`
gives one sharp peak at the symbol phase for the right hypothesis. A hypothesis
is accepted when `peak > 0.12` and `peak / mean(folded) > 3`. The ratio test is
what rejects a CW tone: it correlates at every lag, so its folded curve is flat
(peak 1.0, ratio 1.0). The phase of the summed correlation at the peak is
`-2*pi*eps`, with `eps` the carrier offset in subcarrier spacings (fractional
part only). The search covers N = 64..8192 and G = N/4, N/8, N/16, N/32, using
about 12 symbols per hypothesis.

## Usage

```sh
tools/cpdetect/cpdetect.py --selftest
tools/cpdetect/cpdetect.py capture.cs16 --fs 2500000        # int16 interleaved I/Q
tools/cpdetect/cpdetect.py --uri ip:192.168.2.1 --fs 2500000 --table 3
tools/cpdetect/link_test.py 192.168.2.1 192.168.3.1         # TX board, RX board (cabled)

# on the board (arm binary from app/cpdetect, or the package):
cpdetect -s 2500000          # one analysis of the live RX stream, JSON on stdout
cpdetect -l -i 2 -M          # repeat every 2 s and publish to state/cpdetect/<key>
```

Result line, for example:
`{"samples": 262144, "detected": true, "N": 1024, "G": 128, "peak": 0.9987, "ratio": 5.060, "start": 1075, "eps": 0.0310, "spacing_hz": 2441.41, "symbol_us": 460.80, "frac_cfo_hz": 75.6}`
When nothing passes, `detected` is false and the best hypothesis appears only
under `best_rejected` for diagnosis. MQTT keys (with `-M`): `detected`,
`fft_size`, `guard_samples`, `peak`, `ratio`, `eps`, `spacing_hz`.

## Validation (measured)

Synthetic (`--selftest`): QPSK OFDM, four (N, G) pairs, random symbol offset and
fractional offset, SNR 20 and 5 dB: correct N and G, offset error below 0.01
spacing. Noise, a CW tone, two tones and single-carrier QPSK are rejected.

Hardware: original Pluto TX1 -> 40 dB attenuator -> Pluto+ RX1, LO 2.4 GHz,
RX gain 45 dB manual, 1 s captures over libiio. All of these were found with
the right N and G: OFDM N=1024 G=128 and N=256 G=32 at 2.5 MS/s, N=2048
G=256 at 10 MS/s. At 2.5 MS/s, N=1024 G=128 with the transmit level lowered was
still found at estimated SNR of 13, 3 and -2 dB and not found at -7 dB. A CW
tone, single-carrier QPSK and a noise-only capture (TX at minimum, zero
buffer) were rejected. The C binary gives the same peak, ratio, symbol start
and offset as the Python tool on the same 12 capture files, and found and
rejected the same cases when run on the Pluto+ itself (local libiio) against the
transmitted signals.

Offset check: with a weak CW tone sent in the same transmission as the OFDM
signal, the CP-derived fractional offset agrees with the offset measured from
the tone to within 0.005 subcarrier spacings (N=1024) and 0.014 (N=2048).
The offset between two boards drifts by tens of Hz per minute (about 20 ppm
apart at 2.4 GHz, warming), so repeated captures differ by a few hundredths of a
spacing.

## Limits

- Needs time-domain IQ. Over libiio on Ethernet the stream is continuous only
  up to about 10 MS/s (see `tools/iq_continuity`); above that, run the C
  tool on the board.
- On the board, one analysis of 262144 samples takes about 0.25 s on the
  Pluto+ ARM (measured with a file input; double-precision loop, not
  optimized).
- It detects a cyclic prefix, not a standard: it says OFDM-like with this N
  and G. Short guards (G of a few samples) give high folded peaks on noise;
  only the ratio test keeps them out.
- Only the fractional carrier offset is observable from the prefix.
- Detection needs the signal to fill the capture bandwidth with at least a few
  whole symbols; a burst shorter than about 12 symbols needs `-y`/`--nsym` lowered.
