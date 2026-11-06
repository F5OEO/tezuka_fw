"""template_from_capture.py - cut a version-3 template out of a recorded
/waterfall capture (see replay/capture_frames.py).

The capture is processed the way the daemon does it: a temporal lower
envelope, a smooth receiver baseline (morphological opening, --base-width-hz),
and the residual, live minus baseline, averaged over all frames. The
window centred on --center-hz (absolute, with --fc-hz the capture's centre
frequency) and --width-hz wide is resampled to --bins and written with
hz_per_bin = width / bins, so the device matches it over the same hertz
at any span.

    python3 template_from_capture.py --raw fm.raw --span-hz 30.72e6 --fc-hz 98e6 \
        --center-hz 93.3e6 --width-hz 400e3 --label "FM Broadcast" --template-id 1 \
        --out templates.bin
"""
import argparse, os, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from templates_format import Template, normalize, read_templates, write_templates  # noqa: E402


def opening(x, half):
    from numpy.lib.stride_tricks import sliding_window_view as swv
    p = np.pad(x, half, mode="edge")
    mn = swv(p, 2 * half + 1).min(1)
    p2 = np.pad(mn, half, mode="edge")
    return swv(p2, 2 * half + 1).mean(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", required=True, help="capture_frames.py output")
    ap.add_argument("--floats", type=int, default=4096, help="floats per frame incl. the step counter")
    ap.add_argument("--span-hz", type=float, required=True)
    ap.add_argument("--fc-hz", type=float, required=True, help="capture centre frequency")
    ap.add_argument("--center-hz", type=float, required=True, help="emitter centre frequency")
    ap.add_argument("--width-hz", type=float, required=True, help="template window width")
    ap.add_argument("--bins", type=int, default=512)
    ap.add_argument("--base-width-hz", type=float, default=4e6)
    ap.add_argument("--label", required=True)
    ap.add_argument("--template-id", type=int, required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    a = np.fromfile(args.raw, dtype="<f4").reshape(-1, args.floats)[:, 1:].astype(np.float64)
    n = a.shape[1]
    hz = args.span_hz / n
    env = np.percentile(a, 10, axis=0)
    base = opening(env, max(1, int(args.base_width_hz / hz / 2 + 0.5)))
    resid = np.maximum(a - base, 0).mean(0)

    c = (args.center_hz - args.fc_hz) / hz + n / 2
    w = args.width_hz / hz
    lo, hi = int(round(c - w / 2)), int(round(c + w / 2))
    if lo < 0 or hi > n:
        sys.exit(f"window {lo}..{hi} is outside the frame (0..{n})")
    win = resid[lo:hi]
    x = np.linspace(0, len(win) - 1, args.bins)
    shape = np.interp(x, np.arange(len(win)), win)
    peak_db = 10 * np.log10(win.max() / max(base[lo:hi].mean(), 1e-9))
    print(f"window bins {lo}..{hi} ({hi - lo} live bins, {hz:.0f} Hz/bin), "
          f"peak {peak_db:.1f} dB over baseline, resampled to {args.bins} bins "
          f"({args.width_hz / args.bins:.1f} Hz/bin)")

    tpl = Template(label=args.label, template_id=args.template_id,
                   data=normalize(list(shape)), hz_per_bin=args.width_hz / args.bins)
    templates = []
    if os.path.exists(args.out):
        existing_n, existing = read_templates(args.out)
        if existing_n != args.bins:
            sys.exit(f"{args.out} has {existing_n}-bin templates, not {args.bins}")
        templates = [t for t in existing if t.template_id != args.template_id]
    templates.append(tpl)
    write_templates(args.out, args.bins, templates)
    print(f"wrote {len(templates)} templates to {args.out}")


if __name__ == "__main__":
    main()
