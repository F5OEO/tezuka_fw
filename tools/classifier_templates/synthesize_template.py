#!/usr/bin/env python3
"""synthesize_template.py — generate a canonical PSD-shape template
directly, without needing GNU Radio/TorchSig installed or a real signal
to capture from.

This does NOT simulate actual modulated waveforms (no symbol generation,
no channel model, no noise) — it synthesizes each signal class's
*characteristic PSD envelope* directly in the frequency domain, since
that envelope shape is exactly what the classifier correlates against
(see app/classifier/classifier.c and the README in this directory for
why: magnitude/PSD spectral-shape matching, not true cyclostationary
SCF). That makes this useful for getting the template/correlation
pipeline working end to end with recognizable, distinct shapes, and as
a fallback for signal classes you can't easily capture live — but it's
an idealization, not a substitute for real captures or a proper
GNU Radio/TorchSig synthesis pipeline (varied bandwidth, symbol rate,
roll-off, realistic noise) for a production template library. Prefer
capture_from_device.py against a real signal when you can.

Usage:
    python3 synthesize_template.py --shape ofdm --label LTE --template-id 3 \
        --bins 512 --bw-frac 0.4 --out templates.bin
"""
import argparse

import numpy as np

from templates_format import Template, normalize, write_templates, read_templates

SHAPES = ["ofdm", "narrowband", "chirp", "pulsed", "cw"]


def synth_ofdm(n: int, bw_frac: float) -> np.ndarray:
    """Wideband, roughly-flat-top PSD with raised-cosine-ish rolloff at
    the band edges — the classic OFDM/multicarrier envelope (LTE, 5G NR,
    DAB+, Wi-Fi)."""
    x = np.linspace(-0.5, 0.5, n)
    half_bw = bw_frac / 2
    edge = half_bw * 0.15  # rolloff width
    psd = np.where(
        np.abs(x) <= half_bw - edge,
        1.0,
        np.where(
            np.abs(x) <= half_bw + edge,
            0.5 * (1 + np.cos(np.pi * (np.abs(x) - (half_bw - edge)) / (2 * edge))),
            0.02,  # noise floor outside the band, not literal zero
        ),
    )
    return psd


def synth_narrowband(n: int, bw_frac: float) -> np.ndarray:
    """Narrow, roughly-Gaussian peak — narrowband FM/AM voice, GSM,
    C2000-style digital voice channels, maritime/airband."""
    x = np.linspace(-0.5, 0.5, n)
    sigma = bw_frac / 2.355  # bw_frac ~= FWHM
    psd = np.exp(-0.5 * (x / sigma) ** 2) + 0.02
    return psd


def synth_chirp(n: int, bw_frac: float) -> np.ndarray:
    """Roughly-uniform occupancy across the swept bandwidth with soft
    edges — a chirp spread-spectrum signal's time-averaged PSD is close
    to flat across its sweep range (LoRa)."""
    x = np.linspace(-0.5, 0.5, n)
    half_bw = bw_frac / 2
    edge = half_bw * 0.3
    psd = np.where(
        np.abs(x) <= half_bw,
        1.0 - 0.15 * np.abs(x) / half_bw,  # slight taper, not perfectly flat
        np.exp(-((np.abs(x) - half_bw) / edge) ** 2) * 0.3,
    )
    return psd + 0.02


def synth_pulsed(n: int, bw_frac: float) -> np.ndarray:
    """A short time-domain pulse (ADS-B, radiosonde bursts) has a wide,
    sinc-shaped frequency-domain envelope — main lobe plus visible
    sidelobes, distinct from OFDM's flatter top and cleaner edges."""
    x = np.linspace(-0.5, 0.5, n)
    half_bw = bw_frac / 2
    u = np.pi * x / max(half_bw, 1e-6)
    sinc = np.where(np.abs(u) < 1e-6, 1.0, np.sin(u) / u)
    psd = np.abs(sinc) ** 2
    return psd + 0.01


def synth_cw(n: int, bw_frac: float) -> np.ndarray:
    """A single narrow spike — continuous-wave / beacon-like carriers."""
    x = np.linspace(-0.5, 0.5, n)
    sigma = max(bw_frac / 2.355, 1.0 / n)
    psd = np.exp(-0.5 * (x / sigma) ** 2) + 0.005
    return psd


SYNTH = {
    "ofdm": synth_ofdm,
    "narrowband": synth_narrowband,
    "chirp": synth_chirp,
    "pulsed": synth_pulsed,
    "cw": synth_cw,
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shape", required=True, choices=SHAPES)
    ap.add_argument("--label", required=True, help="class label for this template (e.g. LTE, ADS-B)")
    ap.add_argument("--template-id", type=int, required=True)
    ap.add_argument("--bins", type=int, default=512, help="canonical_n (default: 512)")
    ap.add_argument("--bw-frac", type=float, default=0.3,
                     help="occupied bandwidth as a fraction of the template's span, 0-1 (default: 0.3)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    if not (0 < args.bw_frac <= 1.0):
        raise SystemExit("--bw-frac must be in (0, 1]")

    raw = SYNTH[args.shape](args.bins, args.bw_frac)
    new_template = Template(label=args.label, template_id=args.template_id, data=normalize(list(raw)))

    try:
        existing_n, existing = read_templates(args.out)
        if existing_n != args.bins:
            raise SystemExit(
                f"error: {args.out} already has canonical_n={existing_n}, but --bins={args.bins} — "
                "use the same --bins as the existing file, or write to a new one."
            )
        existing = [t for t in existing if t.template_id != new_template.template_id]
        templates = existing + [new_template]
    except FileNotFoundError:
        templates = [new_template]

    write_templates(args.out, args.bins, templates)
    print(f"Wrote {len(templates)} template(s) to {args.out} (added/updated: {args.label!r}, shape={args.shape}).")


if __name__ == "__main__":
    main()
