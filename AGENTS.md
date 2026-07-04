# AGENTS.md — tezuka_fw

Buildroot Zynq/AD9363 firmware for PlutoSDR, Fishball, LibreSDR, and related SDR boards.
Bundles ADI Linux + U-Boot, Maia-sdr, SoapySDR, libiio, and custom SDR tools.
See [`README.md`](./README.md) for full project overview.

- Repo: https://github.com/F5OEO/tezuka_fw
- Branch: `future` (dev), `main` (release)

## Boards

See `boards.json` for board→defconfig map.

## Build (Locally)

```bash
./getbuildroot.sh
make -C buildroot O=output/<board> <defconfig>
make -C buildroot O=output/<board>
# → output/<board>/images/tezuka.zip
```

### Menuconfig (Docker)

```bash
docker run -dit --name builder ubuntu:22.04
apt-get install -y make gcc g++ bison flex libncurses5-dev bc cpio \
  rsync cmake xz-utils git tmux wget
git clone --depth 1 --branch future https://github.com/F5OEO/tezuka_fw.git
cd tezuka_fw && ./getbuildroot.sh
export BR2_EXTERNAL=$PWD
make -C buildroot O=output/<board> <defconfig>
tmux new -s menuconfig 'make -C buildroot O=output/<board> menuconfig'
# Attach: tmux attach -t menuconfig
```

### CI

Single board (fast, cached):

```bash
gh workflow run main.yml -f boards=<board>
```

All boards:

```bash
gh workflow run main.yml
```

## CI workflow

### Triggers

| Event               | Action                                                    |
| ------------------- | --------------------------------------------------------- |
| `push` tag `v*.*.*` | Build all → GitHub Release                                |
| `workflow_dispatch` | Build selected. Add `-f release=true` to test release job |

### Flow

```
matrix → tezuka (per board, parallel) → release (tag only)
```

Each tezuka job: cache restore → uboot → kernel → rootfs → image → legal-info → SBOM → attest → upload.

Release job: download artifacts → repack zips → attest → `gh release create`.

## SBOM & Attestation

- `make legal-info` → per-package license info
- `cyclonedx-buildroot` → CycloneDX SBOM (JSON + XML)
- `actions/attest@v4` attests `build/<board>.zip` with SBOM (Sigstore)
- `bom/` in artifact: license texts + sbom.json + sbom.xml
- Release zips attested with build provenance

## Pre-commit hooks

Uses `prek`. Config `.pre-commit-config.yaml`:

```bash
prek run --all-files
```

Always `-s` (Signed-off-by).

## PR flow

1. Branch from `future`
2. `prek run --all-files`
3. Push fork
4. PR → `F5OEO/tezuka_fw:future`
5. Test: `gh workflow run main.yml -f boards=<board>`
6. Release: tag `v*.*.*` on `future`

## External overrides

`package/<name>/` shadows built-in Buildroot packages (e.g. `libiio`). `external.mk` includes all `package/*/*.mk`.

## SSH

Discover adress via MDNS (`_iio._tcp.`), user `root` password `analog`.

### Firmware update

SCP `sdimg/` files to `/boot/`, reboot:

```bash
scp sdimg/BOOT.bin root@<host>:/boot/
scp sdimg/uImage root@<host>:/boot/
scp sdimg/uramdisk.image.xz root@<host>:/boot/
scp sdimg/*.dtb root@<host>:/boot/
ssh root@<host> reboot
```

No SFTP. `/boot/` is FAT on SD.

### 1r1t vs 2r2t

AD9361 modes:

| Mode | RX  | TX  | Use                    |
| ---- | --- | --- | ---------------------- |
| 1r1t | 1   | 1   | Default single-channel |
| 2r2t | 2   | 2   | MIMO, diversity        |

Pluto (AD9363) = 1r1t only. FISH Ball (AD9361) can do 2r2t.

Switch:

```bash
iio_attr -u ip:<host> -c ad9361-phy voltage0 rf_port_select
iio_attr -u ip:<host> -d ad9361-phy ensm_mode pinctrl
iio_attr -u ip:<host> -c ad9361-phy voltage0 rf_port_select A_BALANCED
iio_attr -u ip:<host> -c ad9361-phy voltage1 rf_port_select A_BALANCED
```

Caveats: 2r2t halves max sample rate/ch, overflow risk on USB2, FPGA bitstream must support it, higher power, TX LO leakage may differ per ch.

## Key files

| Path                         | Purpose                   |
| ---------------------------- | ------------------------- |
| `configs/<board>_defconfig`  | Board config              |
| `boards.json`                | Board→defconfig for CI    |
| `buildroot.version`          | Pinned Buildroot          |
| `external.mk`                | External package includes |
| `Config.in`                  | External Kconfig          |
| `package/<name>/`            | Custom packages           |
| `board/tezuka/<board>/`      | Overlays, DTS, post-image |
| `.github/workflows/main.yml` | CI                        |
