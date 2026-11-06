"""Tone-continuity check of the libiio RX stream over Ethernet.

Forward link (cabled, 40 dB): TX board sends a cyclic single-sideband tone, RX board is captured with iio_readdev
from the host. The unwrapped phase of the received tone must be a straight line; a dropped block of K samples shows
up as a phase step of 2*pi*f*K/fs. Settings of both boards are restored at the end.
Needs a cabled TX1 -> RX1 link with attenuation between two boards and the libiio network backend to the RX board.

Usage: tone_continuity.py <tx_host> <rx_host> [seconds] [fs:bw,fs:bw,...]
  e.g. tone_continuity.py 192.168.2.1 192.168.3.1 5 2500000:2000000,10000000:8000000
Prints one JSON line per rate: n_phase_jumps is the number of single-sample phase steps above the threshold (0 for a
gapless stream), blk_resid_* the phase residual per 65536-sample block (about 0.01-0.02 rad is noise), sensitivity the
detector's response to samples cut out of the real capture (a working check flags every case). A capture that takes
longer than the real time of its samples (elapsed_s) means the host fell behind the ADC and buffers were lost.
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import numpy as np

TXH, RXH = sys.argv[1:3]
SECS = float(sys.argv[3]) if len(sys.argv) > 3 else 10
OUT = os.environ.get('TONECONT_OUT') or tempfile.mkdtemp(prefix='tone_continuity-')
os.makedirs(OUT, exist_ok=True)
PHY = '/sys/bus/iio/devices/iio:device0'
TONE = 700000
TXATT, RXGAIN = -10, 45
RATES = [(2500000, 2000000), (10000000, 8000000)]
if len(sys.argv) > 4:
    RATES = [tuple(int(v) for v in a.split(':')) for a in sys.argv[4].split(',')]   # fs:bw,...


def ssh(host, cmd, timeout=60):
    r = subprocess.run(['sshpass', '-p', 'analog', 'ssh', '-o', 'StrictHostKeyChecking=no', '-o',
                        'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=5',
                        f'root@{host}', cmd], capture_output=True, timeout=timeout)
    return r.stdout.decode().strip()


ATTRS = ['out_altvoltage0_RX_LO_frequency', 'out_altvoltage1_TX_LO_frequency', 'in_voltage0_gain_control_mode',
         'in_voltage0_hardwaregain', 'out_voltage0_hardwaregain', 'in_voltage_sampling_frequency',
         'in_voltage_rf_bandwidth', 'out_voltage_rf_bandwidth']


def save(host):
    return {a: ssh(host, f'cat {PHY}/{a}').split()[0] for a in ATTRS}


def restore(host, s):
    ssh(host, f'echo {s["in_voltage_sampling_frequency"]} > {PHY}/in_voltage_sampling_frequency; '
              f'echo {s["in_voltage_rf_bandwidth"]} > {PHY}/in_voltage_rf_bandwidth; '
              f'echo {s["out_voltage_rf_bandwidth"]} > {PHY}/out_voltage_rf_bandwidth; '
              f'echo {s["out_altvoltage0_RX_LO_frequency"]} > {PHY}/out_altvoltage0_RX_LO_frequency; '
              f'echo {s["out_altvoltage1_TX_LO_frequency"]} > {PHY}/out_altvoltage1_TX_LO_frequency; '
              f'echo {s["out_voltage0_hardwaregain"]} > {PHY}/out_voltage0_hardwaregain; '
              f'echo {s["in_voltage0_gain_control_mode"]} > {PHY}/in_voltage0_gain_control_mode')
    if s['in_voltage0_gain_control_mode'] == 'manual':
        ssh(host, f'echo {s["in_voltage0_hardwaregain"]} > {PHY}/in_voltage0_hardwaregain')


def setrate(host, fs, bw):
    ssh(host, f'echo {fs} > {PHY}/in_voltage_sampling_frequency; echo {bw} > {PHY}/in_voltage_rf_bandwidth; '
              f'echo {bw} > {PHY}/out_voltage_rf_bandwidth')
    return int(ssh(host, f'cat {PHY}/in_voltage_sampling_frequency'))


def analyse(z, fs, blk=1 << 16):
    """Phase continuity of a tone: mix down with a smooth reference, then fit a line per block; a sample drop shows up
    as a phase step inside a block (residual rms >> noise) and as a single-sample step in the instantaneous frequency."""
    N = 1 << 20
    P = np.abs(np.fft.fft(z[:N] * np.hanning(N))) ** 2
    f0 = np.fft.fftfreq(N, 1 / fs)[np.argmax(P)]
    ph = np.unwrap(np.angle(z * np.exp(-2j * np.pi * f0 * np.arange(len(z)) / fs)))
    nb = len(ph) // blk
    x = np.arange(blk)
    rms = np.empty(nb)
    for i in range(nb):
        seg = ph[i * blk:(i + 1) * blk]
        c = np.polyfit(x, seg, 1)
        rms[i] = (seg - np.polyval(c, x)).std()
    d = np.diff(np.unwrap(np.angle(z)))
    dev = d - np.median(d)
    sig = 1.4826 * np.median(np.abs(dev))
    thr = max(8 * sig, 0.2)
    jumps = np.flatnonzero(np.abs(dev) > thr)
    amp = np.abs(z)
    return dict(f_tone_hz=float(f0), blk_resid_rms_med_rad=float(np.median(rms)), blk_resid_rms_max_rad=float(rms.max()),
                step_sigma_rad=float(sig), jump_thr_rad=float(thr), n_phase_jumps=int(len(jumps)),
                first_jumps=[int(j) for j in jumps[:10]], amp_mean=float(amp.mean()), amp_cv=float(amp.std() / amp.mean()),
                max_abs_raw=float(np.max(np.abs(np.concatenate([z.real, z.imag])))))


def inject(z, k, every):
    """Copy of z with k samples removed at one position (simulated drop)."""
    p = every
    return np.concatenate([z[:p], z[p + k:]])


tx_saved, rx_saved = save(TXH), save(RXH)
print('saved TX', tx_saved, '\nsaved RX', rx_saved, flush=True)
results = []
try:
    ssh(TXH, f'echo 2400000000 > {PHY}/out_altvoltage1_TX_LO_frequency; echo {TXATT} > {PHY}/out_voltage0_hardwaregain')
    ssh(RXH, f'echo 2400000000 > {PHY}/out_altvoltage0_RX_LO_frequency; echo manual > {PHY}/in_voltage0_gain_control_mode; '
             f'echo {RXGAIN} > {PHY}/in_voltage0_hardwaregain')
    for fs_req, bw in RATES:
        ssh(TXH, 'kill $(pidof iio_writedev) 2>/dev/null; sleep 1')
        fs_tx, fs_rx = setrate(TXH, fs_req, bw), setrate(RXH, fs_req, bw)
        assert fs_tx == fs_rx == fs_req, (fs_tx, fs_rx)
        n = next(c for c in (25000, 24000, 24576) if (TONE * c) % fs_req == 0)   # whole number of tone periods
        assert (TONE * n) % fs_req == 0
        t = np.arange(n)
        iq = np.empty(2 * n, dtype='<i2')
        iq[0::2] = np.round(16000 * np.cos(2 * np.pi * TONE * t / fs_req))
        iq[1::2] = np.round(16000 * np.sin(2 * np.pi * TONE * t / fs_req))
        subprocess.run(['sshpass', '-p', 'analog', 'ssh', '-o', 'StrictHostKeyChecking=no', '-o',
                        'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR', f'root@{TXH}', 'cat > /tmp/tc_tone.bin'],
                       input=iq.tobytes(), check=True)
        ssh(TXH, f"printf '%s\\n' 'iio_writedev -c -b {n} cf-ad9361-dds-core-lpc voltage0 voltage1 < /tmp/tc_tone.bin "
                 f"> /tmp/tc_tx.log 2>&1 &' > /tmp/tc_tx.sh; sh /tmp/tc_tx.sh")
        time.sleep(3)
        dds = ssh(TXH, 'for d in /sys/bus/iio/devices/iio:device*; do grep -q dds-core $d/name && echo $d; done')
        txs = ssh(TXH, f'echo writers $(pidof iio_writedev | wc -w) buffer $(cat {dds}/buffer/enable); cat /tmp/tc_tx.log')
        print(f'fs={fs_req}: TX {txs}', flush=True)
        assert txs.startswith('writers 1 buffer 1'), 'TX writer not running'
        gain = ssh(RXH, f'cat {PHY}/in_voltage0_hardwaregain')
        ns = int(fs_req * SECS)
        fn = f'{OUT}/tc_{fs_req}.bin'
        t0 = time.time()
        subprocess.run(['timeout', str(int(SECS * 3 + 30)), 'iio_readdev', '-u', f'ip:{RXH}', '-b', '262144', '-s', str(ns),
                        'cf-ad9361-lpc', 'voltage0', 'voltage1'], stdout=open(fn, 'wb'), stderr=subprocess.DEVNULL)
        el = time.time() - t0
        d = np.fromfile(fn, dtype='<i2').reshape(-1, 2).astype(float)
        z = d[:, 0] + 1j * d[:, 1]
        r = dict(fs=fs_req, rx_gain=gain, samples=len(z), expected=ns, elapsed_s=round(el, 2), tx=txs)
        r.update(analyse(z, fs_req))
        results.append(r)
        print(json.dumps(r), flush=True)
        # sensitivity: drop k samples from the middle of the real capture and re-run the detector
        sens = []
        for k in (1, 7, 100, 1000, 65536):
            zz = inject(z, k, len(z) // 2 + 12345)
            ra = analyse(zz, fs_req)
            sens.append(dict(dropped=k, jumps=ra['n_phase_jumps'], blk_resid_max=round(ra['blk_resid_rms_max_rad'], 3)))
        print('SENS', fs_req, json.dumps(sens), flush=True)
        r['sensitivity'] = sens
        os.remove(fn)
finally:
    ssh(TXH, 'kill $(pidof iio_writedev) 2>/dev/null; sleep 1; rm -f /tmp/tc_tone.bin /tmp/tc_tx.sh /tmp/tc_tx.log')
    restore(TXH, tx_saved)
    restore(RXH, rx_saved)
    print('restored TX', save(TXH), '\nrestored RX', save(RXH))
    json.dump(results, open(f'{OUT}/results.json', 'w'), indent=1)
