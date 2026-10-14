#!/usr/bin/env python3
"""Cabled two-board check of cpdetect: TX board sends OFDM and control signals, the RX board is captured over libiio and
analysed with cpdetect.py.

Needs a cable (with attenuation) from the TX board's TX1 to the RX board's RX1 and the libiio tools on the host. Both
boards' radio settings (rate, bandwidth, LO, gain, mode) are restored at the end.

Usage: link_test.py <tx_host> <rx_host> [--rx-gain 45] [--tx-gain -10]
"""
import argparse
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpdetect import best, ofdm  # noqa: E402

PHY = '/sys/bus/iio/devices/iio:device0'
ATTRS = ['out_altvoltage0_RX_LO_frequency', 'out_altvoltage1_TX_LO_frequency', 'in_voltage0_gain_control_mode',
         'in_voltage0_hardwaregain', 'out_voltage0_hardwaregain', 'in_voltage_sampling_frequency',
         'in_voltage_rf_bandwidth', 'out_voltage_rf_bandwidth']
SSH = ['sshpass', '-p', 'analog', 'ssh', '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
       '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=5']


def ssh(host, cmd, stdin=None):
    return subprocess.run(SSH + [f'root@{host}', cmd], input=stdin, capture_output=True,
                          timeout=60).stdout.decode().strip()


def save(host):
    return {a: ssh(host, f'cat {PHY}/{a}').split()[0] for a in ATTRS}


def restore(host, s):
    ssh(host, '; '.join(f'echo {s[a]} > {PHY}/{a}' for a in (
        'in_voltage_sampling_frequency', 'in_voltage_rf_bandwidth', 'out_voltage_rf_bandwidth',
        'out_altvoltage0_RX_LO_frequency', 'out_altvoltage1_TX_LO_frequency', 'out_voltage0_hardwaregain',
        'in_voltage0_gain_control_mode')))
    if s['in_voltage0_gain_control_mode'] == 'manual':
        ssh(host, f'echo {s["in_voltage0_hardwaregain"]} > {PHY}/in_voltage0_hardwaregain')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('tx_host')
    ap.add_argument('rx_host')
    ap.add_argument('--rx-gain', type=float, default=45)
    ap.add_argument('--tx-gain', type=float, default=-10)
    a = ap.parse_args()
    txh, rxh = a.tx_host, a.rx_host
    rng = np.random.default_rng(7)
    tx_saved, rx_saved = save(txh), save(rxh)

    def start_tx(x, rms):
        x = x / np.sqrt(np.mean(np.abs(x) ** 2)) * rms if rms else x * 0
        iq = np.empty(2 * len(x), dtype='<i2')
        iq[0::2], iq[1::2] = np.round(x.real), np.round(x.imag)
        ssh(txh, 'kill $(pidof iio_writedev) 2>/dev/null; sleep 1')
        ssh(txh, 'cat > /tmp/lt_tx.bin', stdin=iq.tobytes())
        ssh(txh, f"printf '%s\\n' 'iio_writedev -c -b {len(x)} cf-ad9361-dds-core-lpc voltage0 voltage1 "
                 f"< /tmp/lt_tx.bin > /dev/null 2>&1 &' > /tmp/lt_tx.sh; sh /tmp/lt_tx.sh")
        time.sleep(2.5)
        if ssh(txh, 'pidof iio_writedev | wc -w') != '1':
            raise RuntimeError('TX writer not running')

    def capture(fs, n=1 << 19):
        raw = subprocess.run(['iio_readdev', '-u', f'ip:{rxh}', '-b', '262144', '-s', str(n), 'cf-ad9361-lpc',
                              'voltage0', 'voltage1'], capture_output=True, timeout=120).stdout
        d = np.frombuffer(raw, dtype='<i2').reshape(-1, 2).astype(float)
        return d[:, 0] + 1j * d[:, 1]

    rows = []
    try:
        ssh(txh, f'echo 2400000000 > {PHY}/out_altvoltage1_TX_LO_frequency; echo {a.tx_gain} > {PHY}/out_voltage0_hardwaregain')
        ssh(rxh, f'echo 2400000000 > {PHY}/out_altvoltage0_RX_LO_frequency; echo manual > {PHY}/in_voltage0_gain_control_mode; '
                 f'echo {a.rx_gain} > {PHY}/in_voltage0_hardwaregain')
        for fs, bw, sigs in ((2500000, 2000000, [('OFDM', 1024, 128, 600), ('OFDM', 256, 32, 150), ('CW', 0, 0, 0)]),
                             (10000000, 8000000, [('OFDM', 2048, 256, 1200), ('CW', 0, 0, 0)])):
            for h in (txh, rxh):
                ssh(h, f'echo {fs} > {PHY}/in_voltage_sampling_frequency; echo {bw} > {PHY}/in_voltage_rf_bandwidth; '
                       f'echo {bw} > {PHY}/out_voltage_rf_bandwidth')
            for kind, n, g, na in sigs:
                if kind == 'OFDM':
                    start_tx(ofdm(max(1, 115200 // (n + g)), n, g, na, rng), 3000)
                else:
                    start_tx(np.exp(2j * np.pi * 700000 * np.arange(25000) / fs), 6000)
                b, tab = best(capture(fs))
                got = f'N={b["N"]} G={b["G"]} peak={b["peak"]:.2f} ratio={b["ratio"]:.1f} eps={b["eps"]:+.3f}' if b \
                    else f'none (top rejected: ratio {tab[0]["ratio"]:.2f})'
                want = f'N={n} G={g}' if kind == 'OFDM' else 'none'
                ok = (b is not None and (b['N'], b['G']) == (n, g)) if kind == 'OFDM' else b is None
                rows.append(ok)
                print(f'{fs / 1e6:5g} MS/s  {kind:5s} expected {want:14s} -> {got:55s} {"ok" if ok else "FAIL"}', flush=True)
    finally:
        ssh(txh, 'kill $(pidof iio_writedev) 2>/dev/null; sleep 1; rm -f /tmp/lt_tx.bin /tmp/lt_tx.sh')
        restore(txh, tx_saved)
        restore(rxh, rx_saved)
    return 0 if rows and all(rows) else 1


if __name__ == '__main__':
    sys.exit(main())
