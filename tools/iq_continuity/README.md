# iq_continuity - is the libiio RX stream gapless?

`tone_continuity.py` sends a cyclic single-sideband tone from one board to
another over a cable, reads the receiving board with `iio_readdev` from the
host, and checks that the tone's phase is a straight line. A dropped block of K
samples is a phase step of `2*pi*f*K/fs`. The tool also cuts samples out of the
real capture and shows that its detector flags every case, so a clean result is
meaningful. It restores both boards' settings.

```sh
tools/iq_continuity/tone_continuity.py 192.168.2.1 192.168.3.1 5 2500000:2000000,10000000:8000000
```

Notes from building it:

- The tone must hold a whole number of cycles in the cyclic TX buffer (n=25000
  samples at 2.5/10/12.5/20 MS/s with 700 kHz, n=24000 at 15 MS/s, n=24576 at
  30.72 MS/s).
- Do not fit one straight line to the whole capture: two boards drift apart by
  tens of Hz, which is hundreds of radians over seconds. The tool detrends
  per 65536-sample block and watches single-sample steps instead.
- After the TX writer exits the DAC holds its last sample, so a "noise" control
  needs the TX gain at its minimum and a zero buffer, not just a stopped writer.

## Measured: Pluto+ RX over Ethernet (host libiio, `-b 262144`), 3-5 s each

| Rate | Wall time for the data | Phase jumps | Effective rate |
|------|------------------------|-------------|----------------|
| 2.5 MS/s | 5.1 s for 5 s | 0 | real time |
| 10 MS/s | 5.1 s for 5 s | 0 | real time |
| 12.5 MS/s | 3.14 s for 3 s | 4 | 11.9 MS/s |
| 15 MS/s | 3.43 s for 3 s | 30 | 13.1 MS/s |
| 20 MS/s | 4.63 s for 3 s | 203 | 13.0 MS/s |
| 30.72 MS/s | 7.22 s for 3 s | 327 | 12.8 MS/s |

Above about 12 MS/s the host cannot pull more than roughly 50 MB/s, the
board's DMA buffers overrun, and whole buffers are lost: the phase jumps sit
exactly 262144 samples apart. 12.5 MS/s is marginal. USB was not tested.
Continuous host-side analysis is therefore fine up to about 10 MS/s; beyond
that process on the board (see `tools/cpdetect`) or take short bursts.
