# Classifier template tools

Dev-machine tools for building `templates.bin` — the file the onboard
classifier daemon (`app/classifier/classifier.c`) loads from
`/mnt/jffs2/classifier/templates.bin` on the device. **Not** part of the
Buildroot rootfs; run these on your own machine.

A small starter set (FM Broadcast, DAB+, CW/Beacon — the three
synthetic shapes below) ships and is seeded automatically on first boot
(`board/tezuka/common/overlay_tezuka/etc/classifier-default-templates.bin`,
regenerate it with `synthesize_template.py` if you change it — see
`S61classifier`), so a fresh device has *something* to match against
immediately rather than reporting "unknown" until you push your own.
Any `cmd/classifier/templates` push replaces it permanently.

## Quick start: bootstrap from real hardware

Fastest way to get a first working template set, no GNU Radio/TorchSig
required — capture known signals directly off a device that's already
running:

```sh
pip install websockets
python3 capture_from_device.py --host 192.168.1.50 --label OFDM --template-id 1 --out templates.bin
python3 capture_from_device.py --host 192.168.1.50 --label CW    --template-id 2 --out templates.bin
```

Each run captures and averages live `/waterfall` frames while the device
is tuned to a known signal of that type, normalizes the result, and
appends it to `templates.bin` (re-running with the same `--template-id`
replaces that entry rather than duplicating it). All templates in one
file must share the same bin count, so keep the device's span/FFT size
consistent across captures — or write to separate files per span.

Push the result to a device with `cmd/classifier/templates` (base64 of
the file, matching WireGuard's `cmd/vpn/config` transport):

```sh
mosquitto_pub -h 192.168.1.50 -t cmd/classifier/templates -m "$(base64 -w0 templates.bin)"
```

## Quick start: synthesize a canonical shape (no hardware, no GNU Radio)

For signal classes you can't easily capture live yet, or just to get
distinct-looking templates working end to end fast:

```sh
python3 synthesize_template.py --shape ofdm       --label LTE   --template-id 1 --out templates.bin
python3 synthesize_template.py --shape narrowband --label GSM   --template-id 2 --out templates.bin
python3 synthesize_template.py --shape chirp      --label LoRa  --template-id 3 --out templates.bin
python3 synthesize_template.py --shape pulsed     --label ADS-B --template-id 4 --out templates.bin
python3 synthesize_template.py --shape cw         --label Beacon --template-id 5 --out templates.bin
```

**Important caveat**: this generates each class's *characteristic PSD
envelope* directly (flat-top-with-rolloff for OFDM, narrow peak for
narrowband FM/voice, roughly-flat-across-sweep for chirp, sinc-shaped
for pulsed bursts, a tight spike for CW) — it does not simulate actual
modulated waveforms, channel effects, or noise. Verified that the five
canonical shapes are genuinely distinguishable by the real classifier
(cross-compiled `classify()`, run under `qemu-arm-static`: each
synthesized shape matched its own template at score 1.0, no confusion
with the others) — but this is an idealization for bootstrapping the
pipeline, not a substitute for real captures (`capture_from_device.py`)
or a proper GNU Radio/TorchSig synthesis pipeline (varied bandwidth,
symbol rate, roll-off, realistic noise/frequency-offset) for a
production template library covering many instances per class.

## Building a real template library (GNU Radio / TorchSig)

For a proper library — more instances per class, varied bandwidth/symbol
rate/roll-off/frequency-offset — synthesize signals offline with GNU
Radio or TorchSig, average or PCA down to one or a few representative
PSD shapes per class, normalize with `templates_format.normalize()`
(zero-mean, unit L2 norm — the same normalization the live frame gets at
runtime, so template and live vectors are directly comparable), and
write with `templates_format.write_templates()`. This repo doesn't
include that pipeline (it's specific to whatever signal classes and
GNU Radio/TorchSig setup you're targeting); `templates_format.py`'s
`Template`/`write_templates` are the integration point once you have PSD
arrays from your own flowgraphs.

## File format

See the docstring at the top of `templates_format.py` — kept in lockstep
with `classifier.c`'s `templates_load()`. Both sides were cross-validated
directly: a file written by `templates_format.py` was fed to the actual
on-device C parser (cross-compiled, run under `qemu-arm-static`) and
correctly classified a matching signal.
