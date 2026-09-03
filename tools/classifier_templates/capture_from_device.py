#!/usr/bin/env python3
"""capture_from_device.py — bootstrap a template by capturing a live
signal straight off the device's own /waterfall feed, the same one
Dashboard/radioastro.jsx and the on-device classifier daemon consume.

Point this at a Pluto+/Tezuka device that's already tuned to a known
signal (via the Dashboard, or `mosquitto_pub -t cmd/rx/frequency ...`),
capture and average a handful of frames, and append the result as a
template — no GNU Radio/TorchSig install required. This is the fast
path for a first hardware smoke test; build out a proper synthesis-based
template library (see synthesize_template.py) once the basic pipeline
is confirmed working end to end.

By default this also writes a version-2 feature gate (see templates_format.py
and app/classifier/classifier.c's template_gate_factor()): each of the
captured frames (not just their average) is run through the same
bandwidth/PAPR/flatness/peak-count features the on-device daemon computes,
and the range across those per-frame values -- widened a bit further by
--gate-margin -- becomes the template's expected range. That's genuinely
data-driven (actual capture-to-capture spread), unlike
synthesize_template.py's single-idealized-curve range, so the default
margin here is smaller. Pass --no-gate for a plain shape-only (version 1)
template.

Usage:
    python3 capture_from_device.py --host 192.168.1.50 --label OFDM \
        --template-id 1 --frames 20 --out templates.bin

Requires: pip install websockets
"""
import argparse
import asyncio
import struct
import sys

from features import extract_frame_features
from templates_format import FeatureRanges, Template, normalize, write_templates, read_templates

try:
    import websockets
except ImportError:
    websockets = None


async def capture_frames(host: str, port: int, n_frames: int, path: str = "/waterfall"):
    """Connects to ws://host:port/waterfall (same protocol maia-httpd
    serves, same parsing radioastro.jsx and classifier.c both use: the
    binary message is a float32 array where f[0] is a structural step
    index and f[1:] are FFT bins as raw linear amplitude) and returns a
    list of the raw f[1:] bin arrays for n_frames received messages."""
    uri = f"ws://{host}:{port}{path}"
    frames = []
    async with websockets.connect(uri, max_size=None) as ws:
        while len(frames) < n_frames:
            msg = await ws.recv()
            if not isinstance(msg, (bytes, bytearray)):
                continue
            count = len(msg) // 4
            if count < 2:
                continue
            vals = struct.unpack(f"<{count}f", msg)
            frames.append(list(vals[1:]))  # drop the step-index element
    return frames


def average_frames(frames):
    n = len(frames[0])
    if any(len(f) != n for f in frames):
        raise ValueError("frames have inconsistent bin counts (did the span change mid-capture?)")
    return [sum(f[i] for f in frames) / len(frames) for i in range(n)]


def feature_ranges_from_frames(frames, margin: float) -> "FeatureRanges | None":
    """Per-frame (not averaged) features across the actual capture, widened
    by `margin` -- real observed spread, not a guess. Returns None if none
    of the captured frames had enough energy to produce valid features."""
    per_frame = [extract_frame_features(f) for f in frames]
    valid = [ff for ff in per_frame if ff.valid]
    if not valid:
        return None
    return FeatureRanges.from_samples(
        [ff.bandwidth_frac for ff in valid],
        [ff.papr_db for ff in valid],
        [ff.flatness for ff in valid],
        [ff.peak_count for ff in valid],
        margin=margin,
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="device IP/hostname")
    ap.add_argument("--port", type=int, default=80, help="maia-httpd port (default: 80, matches S60maia-httpd)")
    ap.add_argument("--label", required=True, help="class label for this template (e.g. OFDM)")
    ap.add_argument("--template-id", type=int, required=True, help="numeric template id, must be unique within the output file")
    ap.add_argument("--frames", type=int, default=20, help="frames to capture and average (default: 20)")
    ap.add_argument("--gate-margin", type=float, default=0.15,
                     help="fractional margin further widening the observed per-frame feature "
                          "spread into the template's expected range (default: 0.15)")
    ap.add_argument("--no-gate", action="store_true",
                     help="write a plain shape-only (version 1) template, with no feature gate")
    ap.add_argument("--out", required=True, help="templates.bin path to write/append to")
    args = ap.parse_args()

    if websockets is None:
        print("error: the 'websockets' package is required (pip install websockets)", file=sys.stderr)
        sys.exit(1)

    print(f"Capturing {args.frames} frames from ws://{args.host}:{args.port}/waterfall ...", file=sys.stderr)
    frames = asyncio.run(capture_frames(args.host, args.port, args.frames))
    avg = average_frames(frames)
    canonical_n = len(avg)
    print(f"Captured and averaged {len(frames)} frames, {canonical_n} bins each.", file=sys.stderr)

    feature_ranges = None
    if not args.no_gate:
        feature_ranges = feature_ranges_from_frames(frames, args.gate_margin)
        if feature_ranges is None:
            print("warning: no captured frame had enough energy to compute a feature gate "
                  "-- writing a shape-only template instead", file=sys.stderr)

    new_template = Template(label=args.label, template_id=args.template_id,
                             data=normalize(avg), feature_ranges=feature_ranges)

    # Append to an existing file if present (and its canonical_n matches),
    # otherwise start a fresh one — makes it natural to build a template
    # library with repeated invocations against different known signals.
    try:
        existing_n, existing = read_templates(args.out)
        if existing_n != canonical_n:
            print(
                f"error: {args.out} already has canonical_n={existing_n}, "
                f"but this capture has {canonical_n} bins (span/FFT size differs) — "
                "write to a new file or recapture at the same span.",
                file=sys.stderr,
            )
            sys.exit(1)
        existing = [t for t in existing if t.template_id != new_template.template_id]
        templates = existing + [new_template]
        print(f"Appending to existing {args.out} ({len(existing)} template(s) already present).", file=sys.stderr)
    except FileNotFoundError:
        templates = [new_template]

    write_templates(args.out, canonical_n, templates)
    gate_note = "with feature gate" if feature_ranges else "shape-only, no gate"
    print(f"Wrote {len(templates)} template(s) to {args.out} ({gate_note}).", file=sys.stderr)


if __name__ == "__main__":
    main()
