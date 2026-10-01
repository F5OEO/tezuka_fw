"""features.py — the same four per-frame scalar features the on-device
classifier computes, ported to Python so the offline tools can compute a
version-2 template's expected feature ranges.

Kept in lockstep with extract_frame_features() in app/classifier/classifier.c
— same constants, same math, operating on the same input (raw linear
amplitude, *not* the zero-mean/unit-L2-norm shape used for the correlation
score itself). If you change one side, change the other.
"""
import math
from dataclasses import dataclass
from typing import Sequence

# Must match app/classifier/classifier.c's BW_GATE_RATIO / PEAK_PROMINENCE_RATIO.
BW_GATE_RATIO = 0.1          # -10 dB
PEAK_PROMINENCE_RATIO = 0.0316227766016838  # -15 dB, i.e. 10**(-15/10)


@dataclass
class FrameFeatures:
    valid: bool
    bandwidth_frac: float
    papr_db: float
    flatness: float
    peak_count: int
    peak_bin: int


def extract_frame_features(raw: Sequence[float]) -> FrameFeatures:
    """`raw` is linear amplitude (not power, not dB, not normalized) —
    exactly what classifier.c's extract_frame_features() takes."""
    n = len(raw)
    if n == 0:
        return FrameFeatures(False, 0.0, 0.0, 0.0, 0, 0)

    amps = [v if v > 0 else 0.0 for v in raw]
    powers = [a * a for a in amps]
    peak_i = max(range(n), key=lambda i: amps[i])
    peak_amp = amps[peak_i]
    sum_power = sum(powers)

    if peak_amp <= 0.0 or sum_power <= 0.0:
        return FrameFeatures(False, 0.0, 0.0, 0.0, 0, 0)

    peak_power = peak_amp * peak_amp
    mean_power = sum_power / n

    papr_db = 10.0 * math.log10(peak_power / max(mean_power, 1e-20))

    log_powers = [math.log(max(p, 1e-20)) for p in powers]
    geomean_power = math.exp(sum(log_powers) / n)
    flatness = min(geomean_power / max(mean_power, 1e-20), 1.0)

    bw_gate = peak_power * BW_GATE_RATIO
    lo, hi = peak_i, peak_i
    while lo > 0 and powers[lo - 1] >= bw_gate:
        lo -= 1
    while hi + 1 < n and powers[hi + 1] >= bw_gate:
        hi += 1
    bandwidth_frac = (hi - lo + 1) / n

    prom_gate = peak_power * PEAK_PROMINENCE_RATIO
    min_sep = max(1, n // 64)
    count = 0
    last_peak = 0
    have_last = False
    for i in range(1, n - 1):
        if powers[i] < prom_gate:
            continue
        if amps[i] <= amps[i - 1] or amps[i] <= amps[i + 1]:
            continue
        if have_last and (i - last_peak) < min_sep:
            continue
        count += 1
        last_peak = i
        have_last = True

    return FrameFeatures(True, bandwidth_frac, papr_db, flatness, count, peak_i)
