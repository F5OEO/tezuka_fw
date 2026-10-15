#!/usr/bin/env python3
# python3 prof_report.py symbols.txt(arm-linux-nm -n -C output) samples.txt
import sys, bisect
syms = []
for l in open(sys.argv[1]):
    p = l.split(None, 2)
    if len(p) == 3 and p[1] in 'tTwW': syms.append((int(p[0], 16), p[2].strip()))
syms.sort()
addrs = [a for a, _ in syms]
tot = {}; all_ = 0
for l in open(sys.argv[2]):
    a, c = l.split(); a = int(a, 16); c = int(c)
    i = bisect.bisect_right(addrs, a) - 1
    name = syms[i][1] if i >= 0 else '?'
    tot[name] = tot.get(name, 0) + c; all_ += c
for k, v in sorted(tot.items(), key=lambda x: -x[1])[:30]: print('%6.2f%% %7.2fs  %s' % (100.0 * v / all_, v / 1000.0, k[:110]))
print('total %.2f s' % (all_ / 1000.0))
