# Notes for Claude

Cross-build for the board (from app/zynqrx)
  . /media/hp-z2-dev/Variste/Doc/prog/tezuka_fw/output/fishball7020/host/environment-setup
  make -j4
  - The SDK environment-setup exports the cross compiler (CXX=arm-linux-g++); the Makefile builds with it. Result: build/zynqrx (B=<dir> changes the folder).
  - Use -j4 at most, per CLAUDE.md. Put scratch builds under /media/hp-z2-dev/Variste/Doc/tmp, e.g. B=/media/hp-z2-dev/Variste/Doc/tmp/zynqrx-arm, and delete them afterwards.
  - PROF=1 adds a SIGPROF PC sampler (static; use IIO=0 MQTT=0). scripts/prof_report.py resolves its output, because the board has no perf.
  - To check on the host: make -j4 HOST=1 (build-host/zynqrx; IIO=0 MQTT=0 by default, IIO=1 needs libiio-dev).

  Target
  - It's a 32-bit ARM Zynq running Tezuka firmware, with 2× Cortex-A9 and NEON.
  - Guard Zynq-only code with #if defined(__arm__) && !defined(__aarch64__).
  - Avoid AArch64-only intrinsics. DVB-T falls back to plain C++ on this target.

  Spin-off project zynqrx
  - It lives at /media/hp-z2-dev/Variste/Doc/prog/zynqrx. It's a copy of the DVB-S/S2 receiver cut from OnAir dvb-arm.
  - It has src/, include/, tools/zynqrx.cpp and a standalone Makefile. The binary is zynqrx and takes the same options as dvbstool.
  - Git is initialised but nothing is committed yet.
  - It's a copy, so later OnAir receiver changes don't carry over automatically.
  - The cross-build presumably uses the same environment-setup sourced, then make -j4 in that directory.

  Deploying to the board
  - The board is root@10.0.0.65, password: analog (ssh root, password analog).
  - Copy the binary with sshpass -p <pw> scp <binary> root@10.0.0.65:/tmp/.
  - /tmp on the board is a ~370 MB tmpfs. Keep recordings under ~250 MB and delete them afterwards.
  - The shell is busybox, so use time env VAR=x cmd.
  - Run commands over 120 s with nohup in the background and poll the output file.
  - Use --std 2 on the board, or failed DVB-S2 hunts fall into a slow scalar Viterbi.

  Do you want me to build zynqrx now and check its Makefile? I can also save a dedicated zynqrx build memory once that's confirmed.
