#!/usr/bin/env python3
"""Blind cyclic-prefix (CP-OFDM) detector for complex baseband samples.

A CP-OFDM symbol of N useful samples is preceded by a copy of its last G samples, so for a sample i inside the
prefix y[i] equals y[i+N] up to a phase. For a hypothesis (N, G) the product y[i]*conj(y[i+N]) therefore has a
stable phase inside every prefix and averages to zero elsewhere. The detector takes cumulative sums of that product
and of the energy, forms the normalised correlation of every window of G samples

    rho[d] = |sum_{i=d}^{d+G-1} y[i] conj(y[i+N])| / energy

and folds rho modulo the symbol length P = N + G. With the right (N, G) the folded curve has one sharp peak at the
symbol phase. A wrong hypothesis, noise, a CW tone (which correlates at every lag) and single-carrier signals give a
flat curve, which the peak/mean ratio test rejects.

Besides N and G it gives the symbol start (samples, modulo P) and the fractional carrier frequency offset in
subcarrier spacings (the phase of the correlation is -2*pi*eps). Only the fractional part is observable this way.

The same algorithm is implemented in C in app/cpdetect for the board.

Usage:
  cpdetect.py capture.cs16 --fs 2500000          # int16 interleaved I/Q file ('-' for stdin)
  cpdetect.py --uri ip:192.168.2.1 --fs 2500000  # capture with iio_readdev first (needs libiio tools on the host)
  cpdetect.py --selftest                         # synthetic OFDM and negative controls
"""
import argparse
import json
import subprocess
import sys

import numpy as np

DEFAULT_NS = (64, 128, 256, 512, 1024, 2048, 4096, 8192)
DEFAULT_GIS = (4, 8, 16, 32)       # guard = N / gi


def fold_metric(y, n, g):
    """Folded normalised CP correlation for one (N, G).

    Returns (F, Sc, nfull): F[P] mean rho per symbol phase, Sc[P] summed complex correlation per phase, nfull the
    number of whole symbols folded. None if there is not enough data."""
    p_len = n + g
    length = len(y) - n
    if length < 3 * p_len + g:
        return None
    prod = y[:length] * np.conj(y[n:n + length])
    energy = 0.5 * (np.abs(y[:length]) ** 2 + np.abs(y[n:n + length]) ** 2)
    csum = np.concatenate(([0], np.cumsum(prod)))
    cen = np.concatenate(([0], np.cumsum(energy)))
    d_len = length - g + 1
    s = csum[g:g + d_len] - csum[:d_len]
    en = cen[g:g + d_len] - cen[:d_len]
    rho = np.abs(s) / np.maximum(en, 1e-12)
    nfull = d_len // p_len
    if nfull < 2:
        return None
    folded = rho[:nfull * p_len].reshape(nfull, p_len).mean(axis=0)
    sc = s[:nfull * p_len].reshape(nfull, p_len).sum(axis=0)
    return folded, sc, nfull


def detect(y, ns=DEFAULT_NS, gis=DEFAULT_GIS, nsym=12, min_peak=0.12, min_ratio=3.0):
    """Blind search over the hypotheses. Uses about nsym symbols of data per hypothesis.

    Returns all hypotheses, best score first. Each entry: N, G, gi, peak (0..1), ratio (peak / mean of the folded
    curve), start (samples modulo N+G where a CP begins), eps (fractional carrier offset in subcarrier spacings),
    nsym (symbols folded), ok (peak and ratio above the thresholds), score."""
    out = []
    for n in ns:
        for gi in gis:
            g = n // gi
            p_len = n + g
            r = fold_metric(y[:int(nsym * p_len + n)], n, g)
            if r is None:
                continue
            folded, sc, nfull = r
            k = int(np.argmax(folded))
            peak, mean = float(folded[k]), float(folded.mean())
            ratio = peak / mean if mean > 0 else 0.0
            eps = float(-np.angle(sc[k]) / (2 * np.pi))
            out.append(dict(N=n, G=g, gi=gi, peak=peak, ratio=float(ratio), start=k, eps=eps, nsym=nfull,
                            ok=bool(peak > min_peak and ratio > min_ratio),
                            score=float(peak * min(ratio, 12.0) / 12.0)))
    out.sort(key=lambda r: -r['score'])
    return out


def best(y, **kw):
    """(best accepted hypothesis or None, full table)."""
    table = detect(y, **kw)
    ok = [r for r in table if r['ok']]
    return (ok[0] if ok else None), table


def read_cs16(src):
    raw = np.fromfile(src if src != '-' else sys.stdin.buffer, dtype='<i2')
    d = raw[:len(raw) // 2 * 2].reshape(-1, 2).astype(np.float64)
    return d[:, 0] + 1j * d[:, 1]


# ----------------------------------------------------------------------------------------------- test signals
def ofdm(nsym, n, g, n_active, rng, amp=1.0):
    """CP-OFDM with random QPSK on n_active carriers around DC (DC unused), unit rms times amp."""
    ks = np.r_[1:n_active // 2 + 1, n - n_active // 2:n]
    out = []
    for _ in range(nsym):
        x_f = np.zeros(n, complex)
        x_f[ks] = (rng.choice([-1, 1], len(ks)) + 1j * rng.choice([-1, 1], len(ks))) / np.sqrt(2)
        x = np.fft.ifft(x_f) * np.sqrt(n)
        out.append(np.concatenate([x[-g:], x]))
    x = np.concatenate(out)
    return amp * x / np.sqrt(np.mean(np.abs(x) ** 2))


def add_noise(x, snr_db, rng):
    sigma = np.sqrt(np.mean(np.abs(x) ** 2) / 10 ** (snr_db / 10) / 2)
    return x + sigma * (rng.standard_normal(len(x)) + 1j * rng.standard_normal(len(x)))


def selftest():
    rng = np.random.default_rng(1)
    fails = 0
    for n, g, na in ((1024, 128, 600), (2048, 256, 1200), (256, 64, 150), (512, 32, 300)):
        x = ofdm(30, n, g, na, rng)
        off = int(rng.integers(0, n + g))
        eps = float(rng.uniform(-0.45, 0.45))
        y0 = x * np.exp(2j * np.pi * eps * np.arange(len(x)) / n)
        for snr in (20, 5):
            y = add_noise(y0[off:], snr, rng)
            b, _ = best(y)
            start = (-off) % (n + g)
            ok = (b is not None and (b['N'], b['G']) == (n, g) and abs(((b['eps'] - eps + .5) % 1) - .5) < 0.03
                  and min((b['start'] - start) % (n + g), (start - b['start']) % (n + g)) <= 2)
            print(f'  OFDM N={n} G={g} eps={eps:+.2f} SNR={snr:2d} dB: '
                  f'{"ok" if ok else "FAIL"} ({b and (b["N"], b["G"], round(b["eps"], 3))})')
            fails += not ok
    m = 60000
    tone = np.exp(2j * np.pi * 0.0873 * np.arange(m))
    sym = (rng.choice([-1, 1], m // 4) + 1j * rng.choice([-1, 1], m // 4)) / np.sqrt(2)
    negatives = {
        'noise': add_noise(np.zeros(m, complex) + 1e-9, 0, rng),
        'CW tone': add_noise(tone, 20, rng),
        'two tones': add_noise(tone + np.exp(2j * np.pi * 0.21 * np.arange(m)), 20, rng),
        'single-carrier QPSK': add_noise(np.convolve(np.repeat(sym, 4), np.ones(4) / 4, 'same'), 15, rng),
    }
    for name, y in negatives.items():
        b, _ = best(y)
        ok = b is None
        print(f'  negative control {name}: {"ok (rejected)" if ok else "FAIL " + str((b["N"], b["G"]))}')
        fails += not ok
    print('selftest', 'PASSED' if not fails else f'FAILED ({fails})')
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('file', nargs='?', help='int16 interleaved I/Q file, or - for stdin')
    ap.add_argument('--uri', help='capture with iio_readdev from this libiio URI instead of reading a file')
    ap.add_argument('--device', default='cf-ad9361-lpc')
    ap.add_argument('--fs', type=float, help='sample rate in Hz (for spacing and offset in Hz)')
    ap.add_argument('--samples', type=int, default=1 << 18, help='samples to use when capturing (default 262144)')
    ap.add_argument('--nsym', type=int, default=12, help='symbols folded per hypothesis')
    ap.add_argument('--table', type=int, default=0, help='also print the top N hypotheses')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.uri:
        raw = subprocess.run(['iio_readdev', '-u', a.uri, '-b', str(a.samples), '-s', str(a.samples), a.device,
                              'voltage0', 'voltage1'], capture_output=True, check=True).stdout
        d = np.frombuffer(raw, dtype='<i2').reshape(-1, 2).astype(np.float64)
        y = d[:, 0] + 1j * d[:, 1]
    elif a.file:
        y = read_cs16(a.file)
    else:
        ap.error('give a file, --uri or --selftest')
    b, table = best(y, nsym=a.nsym)
    res = {'samples': len(y), 'detected': b is not None}
    top = b or table[0]
    fields = {k: top[k] for k in ('N', 'G', 'peak', 'ratio', 'start', 'eps')}
    if a.fs:
        fields['spacing_hz'] = a.fs / top['N']
        fields['symbol_us'] = (top['N'] + top['G']) / a.fs * 1e6
        fields['frac_cfo_hz'] = top['eps'] * a.fs / top['N']
    if b is not None:
        res.update(fields)
    else:
        res['best_rejected'] = fields      # for diagnosis only: failed the peak/ratio thresholds
    print(json.dumps(res))
    for r in table[:a.table]:
        print('  ', json.dumps({k: (round(v, 4) if isinstance(v, float) else v) for k, v in r.items()}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
