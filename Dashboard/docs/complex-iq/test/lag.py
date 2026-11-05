#!/usr/bin/env python3
"""Count the /waterfall frames that still show the old LO after an MQTT retune.

  lag.py RXHOST

A strong tone must be received (it is followed by its peak bin). The script
connects to ws://RXHOST/waterfall, publishes cmd/rx/frequency alternately
2400.5 and 2400.0 MHz through ssh (root/analog, sshpass) six times and prints,
for every retune, the peak bins of the last frames before the command and
(arrival time after the command, peak bin) of the frames after it. The tone
moves by OFS/(fs/4096) bins when the LO moves, so frames still at the old bin
are stale. Needs: numpy, websockets, sshpass. The boards are not restored,
reboot them or set the LO back afterwards.
"""
import asyncio
import subprocess
import sys
import time

import numpy as np
import websockets

RX = sys.argv[1]
SSH = ['sshpass', '-p', 'analog', 'ssh', '-o', 'StrictHostKeyChecking=no',
       '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR',
       f'root@{RX}']


async def main():
    frames = []   # (arrival time, peak bin)
    async with websockets.connect(f'ws://{RX}/waterfall', max_size=None) as ws:
        async def reader():
            async for m in ws:
                if isinstance(m, bytes) and len(m) >= 16384:
                    a = np.frombuffer(m, '<f4')[1:].copy()
                    a[2040:2056] = 0
                    frames.append((time.time(), int(a.argmax())))
        task = asyncio.create_task(reader())
        await asyncio.sleep(4)
        events = []
        for lo in (2400500000, 2400000000) * 3:
            t0 = time.time()
            await asyncio.to_thread(
                subprocess.run, SSH + ['mosquitto_pub -h 127.0.0.1 -t '
                                       f'cmd/rx/frequency -m {lo}'])
            events.append((t0, time.time(), lo))
            await asyncio.sleep(6)
        task.cancel()
    for (t0, t1, lo), nxt in zip(events, events[1:] + [(1e18, 1e18, 0)]):
        pre = [b for t, b in frames if t < t0][-3:]
        post = [(round(t - t1, 2), b) for t, b in frames if t1 <= t < nxt[0]]
        print(f'LO -> {lo / 1e6:.1f}: peak bins before {pre}; '
              f'frames after the command (s, bin): {post[:6]}')

asyncio.run(main())
