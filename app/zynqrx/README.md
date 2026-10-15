# zynqrx

DVB-S and DVB-S2 demodulator for 32-bit ARM (Zynq, 2x Cortex-A9 + NEON). Command line only, no dependencies beyond the C++ standard library and pthreads.

Taken from [OnAir](../OnAir) (branch `dvb-arm`, commit 444d411) and cut down to the DVB-S/S2 receiver, so it builds on its own. GPL-3.0 like OnAir (see `LICENSE`).

## Build

    . <buildroot sdk>/host/environment-setup      # e.g. tezuka_fw/output/fishball7020/host/environment-setup
    make -j4                                      # build/zynqrx, a 32-bit ARM hard-float binary (use at most -j4)
    make -j4 HOST=1                               # the same code for this computer (build-host/zynqrx), for tests
    make -j4 PROF=1                               # static build with a 1 kHz PC sampler: DVBS_PROF=out.txt ./zynqrx ...; scripts/prof_report.py

## Use

    zynqrx gen out.cs8 [--secs 10] [--rate 10] [--std s|s2] [--mod qpsk|8psk|16apsk|32apsk] [--fec 2/3] [--rs 5] [--roll 0.35]
                       [--snr 30] [--cfo 0] [--ppm 0] [--prefiltered] ...      test signal (see the top of tools/zynqrx.cpp)
    zynqrx rx recording --rate <Msps> --rs <Msym/s> [--std 2] [--roll 0.35] [--prefiltered] [--center <Hz>] [--ts out.ts]
    zynqrx bench [gen options] [--blocking] [--realtime] [--hint]               receiver speed on a generated signal
    zynqrx ranges

Recordings are complex baseband, `cs8` (default), `cu8` or `cf32`.

For the Zynq, with a radio that already does the root raised cosine filter and decimation to about 2 samples per symbol:
`--prefiltered` skips the resampler and matched filter, `--center` (with `--rs`) skips the spectrum search and its FFTs, and `--std 2`
searches for DVB-S2 only. The receiver then runs from 100 ksps.

## Measured on the Zynq (single 12 s runs paced to real time, signal 0.5 dB above the standard's Es/N0 threshold unless noted)

| Signal | Result |
|---|---|
| QPSK 2/3, 0.5 Msym/s | all 182 frames, at the edge of real time |
| QPSK 2/3, 0.6 Msym/s | decodes, but 17% slower than real time |
| 8PSK 2/3, 0.3 Msym/s | all 162 frames |
| 8PSK 2/3, 0.4 Msym/s | over capacity, 24 of 218 frames dropped |
| QPSK, 1.5 Msym/s, +6 dB | 2.7x too slow: about 3.9 CPU-s per signal-second on 2 cores |

Cost is about 2.6 CPU-s per Msym/s with the symbol timer (22%), phase refinement (19%), lock hunt, carrier loop and LDPC as the main parts.
Going past about 1 Msym/s needs symbol-rate input from the FPGA (no mixer, timer or halfband on the CPU).

## 32-bit ARM specifics

Two decoder threads, a 64-frame decoder queue, 8-bit LDPC with a NEON layer kernel (rates from 2/3 alone; lower rates try 8 iterations first, adaptively),
NEON QPSK phase refinement. `DECT2_LDPC_Q8_MIN_RATE` and `DECT2_LDPC_Q8_QUICK=0|1` override the LDPC choice. The DVB-S Viterbi decoder is plain C++ here and slow.

## Layout

    src/        receiver sources and private headers
    include/    public headers (dect2/...)
    tools/      zynqrx.cpp, the command line program
    scripts/    PC sampler and its report script
