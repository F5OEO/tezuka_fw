"""tally_results.py - summarize a replay run's state/classifier/result lines.

    classifier -B none -f templates.bin -r out.raw | python3 tally_results.py 30 60

Prints the label counts inside [t_on, t_off) ("ON") and outside it after
the first 12 s ("OFF", skipping the background learning period).
"""
import json, sys
from collections import Counter

t_on, t_off = float(sys.argv[1]), float(sys.argv[2])
rows = []
for line in sys.stdin:
    if "/result " not in line:
        continue
    t, _topic, js = line.split(" ", 2)
    rows.append((float(t), json.loads(js)))
on = [d for t, d in rows if t_on <= t < t_off]
off = [d for t, d in rows if t >= 12 and not t_on <= t < t_off]


def fmt(ds):
    c = Counter(d["label"] for d in ds)
    return " ".join(f"{k}={v}/{len(ds)}" for k, v in c.most_common()) or "-"


snr = sorted(d.get("snr_db", d.get("best_snr_db", 0.0)) for d in on)
print(f"ON: {fmt(on)} (median snr {snr[len(snr) // 2] if snr else 0:.1f} dB) | OFF: {fmt(off)}")
