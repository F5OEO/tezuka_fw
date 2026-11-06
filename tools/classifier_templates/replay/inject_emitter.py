"""inject_emitter.py - add a synthetic emitter to a recorded frame file.

Takes a capture from capture_frames.py and adds, during [t_on, t_off)
seconds at the given frame rate, a signal shaped like template
`tpl_idx` of templates.bin at bin `start`, with its peak `snr_db` above
the median level of that window in the first 10 s of the capture. Each
frame gets independent ~25% per-bin fluctuation (gamma, k=16), roughly
what the averaged spectrometer output shows for a steady signal.

    python3 inject_emitter.py frames.raw out.raw templates.bin 0 600 10 30 60 [fps floats span_hz]

A version-3 template is stretched to its real width on the live grid
(span_hz / (floats - 1) hertz per bin, default 30.72 MHz / 4095).

Replaying out.raw through the classifier and tallying the result topic
(tally_results.py) gives a hardware-free true-positive test: the label
during the ON window should be the injected template's, and the OFF
part should classify exactly like the original capture.
"""
import os, sys
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from templates_format import read_templates  # noqa: E402

src, out, tpl_path = sys.argv[1], sys.argv[2], sys.argv[3]
tpl_idx, start, snr_db = int(sys.argv[4]), int(sys.argv[5]), float(sys.argv[6])
t_on, t_off = float(sys.argv[7]), float(sys.argv[8])
fps = float(sys.argv[9]) if len(sys.argv) > 9 else 7.5
floats = int(sys.argv[10]) if len(sys.argv) > 10 else 4096
span_hz = float(sys.argv[11]) if len(sys.argv) > 11 else 30.72e6

a = np.fromfile(src, dtype="<f4").reshape(-1, floats).copy()
n, tpl = read_templates(tpl_path)
shape = np.array(tpl[tpl_idx].data, dtype=np.float64)
shape -= shape.min()
shape /= shape.max()
# a version-3 template covers n * hz_per_bin hertz: stretch it onto the live grid
if tpl[tpl_idx].hz_per_bin > 0:
    live_hz = span_hz / (floats - 1)
    n_live = max(8, int(round(n * tpl[tpl_idx].hz_per_bin / live_hz)))
    shape = np.interp(np.linspace(0, n - 1, n_live), np.arange(n), shape)
    n = n_live
win = a[:, 1 + start:1 + start + n]
noise = np.median(win[: int(fps * 10)])
peak = noise * 10 ** (snr_db / 10)
rng = np.random.default_rng(1)
for i in range(a.shape[0]):
    if t_on <= i / fps < t_off:
        a[i, 1 + start:1 + start + n] += (peak * shape * rng.gamma(16.0, 1.0 / 16.0, n)).astype("<f4")
a.astype("<f4").tofile(out)
print(f"{a.shape[0]} frames; {tpl[tpl_idx].label!r} at bins {start}..{start + n}, "
      f"{snr_db} dB over local level, {t_on}-{t_off} s -> {out}")
