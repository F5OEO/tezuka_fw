# Validate maia-kmod crash fix on nano hardware

## Solution approach

Trigger CI build for nano board, deploy via MSD, run automated reboot and soak
testing via SSH. A reusable Python test harness (`harness.py`) drives the device
node tests, reboot cycling, and soak monitoring. All scripts live under
`goals/validate-maia-kmod-crash-fix/`.

---

## Steps

### 1. Trigger CI build for nano

```bash
cd /Users/tom/src/uhd/tezuka_fw
gh workflow run main.yml -f boards=nano --repo gretel/miyazaki
```

**Verification:** Workflow run appears in `gh run list`. Note the run ID.

### 2. Wait for build and download artifact

```bash
gh run watch <run-id> --exit-status --repo gretel/miyazaki --interval 60
gh run download <run-id> -n build-nano --repo gretel/miyazaki
```

**Verification:** `build-nano/` directory exists with `flash/pluto.frm` inside.

### 3. Deploy firmware via MSD

- Connect nano to macOS via USB
- MSD volume appears (labeled "NANO" or similar)
- Copy `build-nano/flash/pluto.frm` to the MSD volume
- Eject the MSD volume from macOS (Finder or `diskutil eject`)
- Device auto-reboots and flashes

**Verification:** Device becomes pingable on expected IP after ~60s.
Check serial via `screen /dev/tty.usbmodem* 115200` for boot messages.

### 4. Write test harness

File: `goals/validate-maia-kmod-crash-fix/harness.py`

A reusable Python script with subcommands:
- `check` — open recording + rxbuffer dev nodes, mmap, ioctl, munmap, close.
  Reports success or failure. Reads dmesg for warnings.
- `reboot-cycle` — repeat N times: `ssh reboot`, wait for device back,
  `dmesg | grep -i panic`, accumulate results.
- `soak` — monitor device via SSH + IIO for 90 min, periodic health checks.
- `flash` — (optional) automate MSD copy step.

Write to `goals/validate-maia-kmod-crash-fix/harness.py`.

**Verification:** `python3 -u harness.py check` runs without error on a live
device.

### 5. Run device-node check

```bash
python3 -u goals/validate-maia-kmod-crash-fix/harness.py check
```

Expected output:
```
[OK] /dev/maia-sdr-recording: open → mmap → munmap → close
[OK] /dev/maia-sdr-rxbuffer: open → mmap → ioctl(CACHEINV) → munmap → close
[OK] dmesg clean (0 warnings)
```

**Verification:** Fact 5 ✅ and Fact 6 ✅

### 6. Run reboot cycle test

```bash
python3 -u goals/validate-maia-kmod-crash-fix/harness.py reboot-cycle --count 5
```

Expected: 5/5 clean reboots, each verified via SSH + dmesg after boot.

**Verification:** Fact 3 ✅

### 7. Run soak test

```bash
nohup python3 -u goals/validate-maia-kmod-crash-fix/harness.py soak --duration 90 &
```

Write periodic status to a temp file. Monitor with `schedule_prompt`.

**Verification:** Fact 4 ✅ — no kernel splats, SSH reachable for 90 min.

---

## Files touched

| File | Action |
|------|--------|
| `goals/validate-maia-kmod-crash-fix/harness.py` | **Create** — reusable test harness |
| `build-nano/flash/pluto.frm` (via CI) | Download artifact |

## Risks

- **CI failure unrelated to patch:** Buildroot CI has ~15-30min window, may fail
  on network flake or cache miss. Rerun if spurious.
- **MSD not appearing on macOS:** Some nano boards have USB enumeration quirks.
  Fallback: DFU mode via `dfu-util -a 1 -D pluto.dfu`.
- **Device doesn't boot after flash:** If U-Boot env corrupted, need DFU recovery.
  Known recovery procedure documented in AGENTS.md.
- **Serial vs SSH:** nano is QSPI-only, no Ethernet. Reachable via USB (CDC ECM or
  RNDIS). SSH works over USB networking. If `usb_ethernet_mode` needs changing,
  use `fw_setenv usb_ethernet_mode ecm` (macOS needs ECM).
