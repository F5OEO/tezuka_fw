# CLAUDE.md

## Communication
- Always respond in English, even if the prompt is written in French.
This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

**tezuka_fw** is a Buildroot `BR2_EXTERNAL` tree that builds firmware for Zynq-7000/AD9363 SDR boards (PlutoSDR family and compatible hardware). It is not a standalone project — it requires Buildroot alongside it and acts purely as an overlay of board definitions, packages, and configs.

## Setup (first time)

```bash
./getbuildroot.sh        # downloads Buildroot 2026.02 into buildroot/
source sourceme.first    # sets BR2_EXTERNAL; required before any manual make
```

`buildroot.version` is the single source of truth for the Buildroot version and checksum.

## Build commands

```bash
# Build one board
./build.sh fishball

# Build multiple boards
./build.sh pluto plutoplus e310

# Build all boards
./build.sh all

# With parallel jobs and clean output first
./build.sh -j8 -c fishball

# List all boards and their defconfigs
./build.sh -h
```

Output lands in `output/<board>/images/`, and `build/<board>.zip` is produced automatically.

**Supported boards** are defined in `boards.json` (single source of truth for both `build.sh` and CI):
`pluto`, `plutoplus`, `e200`, `e310`, `libre`, `fishball`, `fishball7020`, `fishball_mini`, `fishball_mini_7020`, `nano`, `plutoskyr2`, `signalsdrpro`

### Manual Buildroot invocation

It depends of board name.

```bash
source sourceme.first
make -C buildroot O=output/pluto pluto_maiasdr_defconfig && make -C buildroot O=output/pluto
```

### CMake policy workaround

For CMake 3.27+ compatibility with older packages:
```bash
CMAKE_POLICY_VERSION_MINIMUM=3.5 make -C buildroot O=output/<board>
```

## Architecture

### BR2_EXTERNAL layout

| Path | Purpose |
|------|---------|
| `configs/<board>_defconfig` | Buildroot defconfigs, one per board |
| `board/tezuka/common/` | Shared scripts, kernel config, U-Boot config, rootfs overlays |
| `board/tezuka/<board>/` | Board-specific DTS, U-Boot DTS, bitstream, patches, uboot-env |
| `package/<name>/` | Custom Buildroot packages (`.mk` + `Config.in`) |
| `app/` | Host-side C utilities for development/testing |
| `tools/` | Development helper scripts |

### Firmware image flow (`board/tezuka/common/post-image.sh`)

`post-image.sh` runs after the Buildroot image step and produces:
- **`pluto.frm` / `pluto.dfu`** — flash-update files (IIO FIT image + MD5)
- **`boot.frm` / `boot.dfu`** — U-Boot + FSBL for flash update
- **`sdimg/`** — SD card boot files: `BOOT.bin` (FSBL+bitstream+U-Boot), `uImage`, `uramdisk.image.xz`, `devicetree.dtb`, `uEnv.txt`
- **`tezuka.zip`** — all of the above, copied to `build/<board>.zip`

Boot process: FSBL → U-Boot → Linux (ADI kernel fork) → rootfs (cpio.xz ramdisk).

### Rootfs overlays (layered, applied in order in defconfig)

1. `overlay_base` — base init scripts, networking, USB gadget
2. `overlay_tezuka` — tezuka-specific services (mosquitto, gpsd, NFS mount, sweep scripts)
3. `overlay_maia` — Maia-SDR integration
4. `overlay_iqengine` — IQEngine integration

### FPGA bitstream management

Bitstreams are binary blobs stored in git at `board/tezuka/<board>/bitstream/maia-iio/system_top.xsa`. The `board-fpga` package extracts `system_top.bit` from the `.xsa` zip during build.

Updated bitstreams are built from a separate project, the user's `maia-sdr` fork, checked out at `/home/hp-z2-dev/prog/maia-sdr` (origin: `F5OEO/maia-sdr`, upstream: `maia-sdr/maia-sdr`).

```bash
# In the maia-sdr checkout
source /home/hp-z2-dev/prog/maia-sdr/sourceme.first   # Vivado 2023.1, oss-cad-suite PATH, ADI_IGNORE_VERSION_CHECK

# Build bitstreams for all boards
make -C /home/hp-z2-dev/prog/maia-sdr/maia-hdl/projects

# Build a single board
cd /home/hp-z2-dev/prog/maia-sdr/maia-hdl/projects/tezuka && PROJECT_NAME=<board> make
```

Copy the resulting `.xsa` into `board/tezuka/<board>/bitstream/maia-iio/system_top.xsa` in this repo, then test it:

```bash
make -C buildroot O=output/<board> board-fpga-reconfigure   # re-extract system_top.bit into BINARIES_DIR
make -C buildroot O=output/<board>                          # rebuild final images via post-image.sh
```

### DATV backend (pluto-ori-ps)

The DATV/DVB-S2 MQTT command and streaming backend behind the Dashboard's DATV Controller page (`cmd/pluto/<call>/...` / `dt/pluto/<call>/...` topics) is a separate project, not part of this repo: `pluto-ori-ps`, branch `tezukadvb` (https://github.com/F5OEO/pluto-ori-ps/tree/tezukadvb), checked out at `/home/hp-z2-dev/prog/pluto-ori-ps`. It builds `pluto_mqtt_ctrl` (generic IIO/MQTT control) and `pluto_stream` (DVB-S2 TX/RX streaming), started at boot by `S96plutostream`. `board/tezuka/common/overlay_tezuka/root/api_controller.sh` is a separate, unrelated MQTT handler for the non-call-rooted `cmd/#`/`state/#` tree (rx/tx frequency, gain, sweep, etc.) — it does not touch the DATV topic tree.

### Cross-compiling companion projects with the Buildroot SDK

To cross-compile an external project (e.g. pluto-ori-ps) against this firmware's toolchain and libraries, source a board's generated SDK environment rather than hand-rolling a toolchain:

```bash
source output/<board>/host/environment-setup
```

This exports `CC`/`CXX` (`arm-linux-gcc`/`arm-linux-g++`), `CROSS_COMPILE=arm-linux-`, and `STAGING_DIR` pointing at that board's sysroot (`output/<board>/host/arm-buildroot-linux-gnueabihf/sysroot`), which already contains this firmware's built libraries and headers (libiio, mosquitto, libgse, NE10, civetweb, ...) — no separate SDK install needed.

Pick a board whose output already has the dependencies the external project needs — not every board builds every optional package. For pluto-ori-ps (needs libgse, NE10, civetweb, libiio, mosquitto): `output/pluto` is missing libgse/NE10 (`package/pluto_stream` in this repo isn't wired into any board defconfig yet), but `output/fishball7020` already has all of them built.

```bash
cd /home/hp-z2-dev/prog/pluto-ori-ps
source /media/hp-z2-dev/Variste/Doc/prog/tezuka_fw/output/fishball7020/host/environment-setup
make pluto_stream   # or: make   (builds pluto_mqtt_ctrl, pluto_stream, iio_ws_proxy)
```

### Testing a binary on a physical board (scp/ssh)

For quick verification without a full firmware reflash, there's a physical test board reachable over the network — root/analog over SSH (password auth). The board's IP can change between sessions (DHCP), so confirm it with the user rather than assuming a previously-used address still applies.

The on-target binary is usually already running, so `scp` straight to `/usr/bin/<binary>` fails with "Text file busy". Copy to a temp name and `mv` into place instead (an atomic rename over a running executable is fine on Linux — the old process keeps its already-open inode):

```bash
ssh-keygen -f ~/.ssh/known_hosts -R "<board-ip>"        # boards regenerate their host key on reflash
sshpass -p analog scp <binary> root@<board-ip>:/usr/bin/<binary>.new
sshpass -p analog ssh root@<board-ip> "cp -n /usr/bin/<binary> /usr/bin/<binary>.orig; \
  chmod 755 /usr/bin/<binary>.new && mv /usr/bin/<binary>.new /usr/bin/<binary>"
```

The `S96plutostream` init script's `stop()` is a no-op (just echoes "Stopping"), so restart the process manually after replacing the binary:

```bash
sshpass -p analog ssh root@<board-ip> "pkill <binary>; nohup /usr/bin/<binary> >/tmp/<binary>.log 2>&1 </dev/null & disown"
```

### Testing the Dashboard against a physical board

`Dashboard/data.jsx` has a dev-host override right at the top:

```js
const MQTT_DEV_HOST = '<board-ip>';   // set to the board's IP for local development; null = window.location.hostname (on-device)
```

This is a working-tree-only value (not meant to be committed as a real IP) — check `git diff Dashboard/data.jsx` first, since it may already carry someone else's in-progress local value; ask before overwriting/reverting it. Setting it points the browser's MQTT WebSocket connection (`ws://<board-ip>:9001/mqtt`) at the physical board instead of `window.location.hostname`, so the Dashboard can be served from a laptop while driving a real board's MQTT broker.

```bash
cd Dashboard && python3 -m http.server 8099
# open http://localhost:8099/Tezuka%20Dashboard.html#datv
```

Combined with a Chrome automation tool, this gives an end-to-end UI test path: drive a control (e.g. the DATV Controller's Stream mode dropdown or PTT button) in the browser, then confirm the effect on real hardware over SSH (e.g. `cat /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown`) — see "Testing a binary on a physical board" above for SSH access. Stop the local server (`pkill -f "http.server 8099"`) and restore any board/UI state changed during the test when done.

Since this is live hardware (not disposable CI infra), confirm with the user before overwriting a running service binary, and back up the original first as shown above.

### Key custom packages

| Package | Description |
|---------|-------------|
| `board-fpga` | Extracts FPGA bitstream from board-local `.xsa` |
| `maia-httpd` | Rust web server (cross-compiled for armv7, branch `sweep`) |
| `maia-wasm` / `maia-kmod` | Maia-SDR WebAssembly UI and kernel module |
| `tezuka_tools` | On-target shell utilities |
| `sdr_usb_gadget` | USB gadget configuration (SDR + audio + mass storage) |
| `civetwebws` | CivetWeb with WebSocket support |

### CI

`.github/workflows/main.yml` builds a matrix from `boards.json`. On tag push (`vX.Y.Z`), it publishes a GitHub release with per-board zips and git-cliff release notes. Manual dispatch builds artifacts only (no release). ccache is keyed per board; Buildroot downloads are shared across the matrix.

## Important constraints

- Never read, grep, or glob inside the `buildroot/` directory — it is a downloaded third-party tree.
- `boards.json` must stay in sync with `configs/` — adding a new board requires both a `*_defconfig` in `configs/` and an entry in `boards.json`.
- The Buildroot version is pinned in `buildroot.version`; bump both `BR_VERSION` and `BR_SHA256` together.

## Verification (always do this before marking a task done)
## Verification (always do this before marking a task done)

- Kernel/rootfs: confirm `make` completes without errors AND the generated
  image has the expected size (Buildroot post-image failures are often silent)
- DTB: validate with `fdtdump` / `dtc -I dtb -O dts` after any device tree change
- `bootgen` (Xilinx tool) is required for image assembly. If the host-built `bootgen` is broken, `bootgen-xlnx` system package is the fallback (`sudo apt install bootgen-xlnx`).

## Git
- Do not mention Claude as author or co-author in commit messages (no
  "Co-Authored-By: Claude" trailer, no "Generated with Claude Code" line).
- Stage only files you changed. Commit/push only when asked.
- ASCII, no embedded double-quotes



