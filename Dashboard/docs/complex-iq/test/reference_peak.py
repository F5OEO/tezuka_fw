# configure + transmit a known signal, then decode /iq-waterfall independently (python) for comparison with the browser
import sys, asyncio, json, numpy as np
sys.path.insert(0, '/home/mb/code/maia-sdr-bp/maia-hdl/hwtest')
import hwlib
tx, rx, fs, what = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
LO = 2_400_000_000
hwlib.configure(tx, rx, fs, min(int(0.8*fs), 50_000_000), LO, int(sys.argv[5]) if len(sys.argv)>5 else 40)
ks = {'tone': [2050], 'two': [2050, 2101]}[what]
ft = [hwlib.tone_freq(fs, k) for k in ks]
hwlib.start_tx(tx, sum(hwlib.tone(fs, f) for f in ft), 6000)
import time; time.sleep(1)
fr = asyncio.run(hwlib.grab_ws(rx, 'iq-waterfall', 3))
fr = [m for m in fr if isinstance(m, bytes) and len(m) == 4*hwlib.FRAME]
c = hwlib.iq_words(fr)
db = 20*np.log10(np.maximum(np.abs(c), 0.5))
db[:, 2046:2051] = -1e9
pk = db.argmax(1)
k = int(np.bincount(pk).argmax())
sel = db[:, k]
fk = LO + (k-2048)/4096*fs
# second tone: largest bin >=4 bins away from the peak
d2 = db.mean(0).copy(); d2[max(0,k-4):k+5] = -1e9; k2 = int(d2.argmax())
print(json.dumps(dict(fs=fs, frames=len(c), tone_hz=ft, peak_bin=k, peak_f_mhz=round(fk/1e6,6),
    offset_khz=round((fk-LO-ft[0])/1e3,3) if what=='tone' else None, db_mean=round(float(sel.mean()),2), db_std=round(float(sel.std()),2),
    second_bin=k2, second_f_mhz=round((LO+(k2-2048)/4096*fs)/1e6,6), second_db=round(float(d2[k2]),2),
    ratio_db=round(float(sel.mean()-d2[k2]),2))))
