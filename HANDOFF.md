# Handoff — 2026-07-05

## Branch

`fix/maia-kmod-crash-v2` → `gretel/miyazaki` (origin), target `F5OEO/tezuka_fw:future`

9 commits on top of `a2d21fc` + 1 kernel patch:

```
e09ea15 fix(nano): remove PANIC_ON_OOPS — was destroying crash evidence
4bee22e fix(nano): memory corruption detection — SLUB_DEBUG, DEBUG_LIST, PANIC_ON_OOPS
06e5494 fix(nano): increase CMA from 16MB to 32MB for maia-sdr DMA
4cbccde ci: split kernel build cache from main output cache
d6b391f fix(ci): force kernel re-install after kernel cache hit
134f11c fix(ci): robust kernel build dir detection for caching
fa073e1 fix(ci): close unterminated string in cache summary echo
c8cb879 fix(build): use curl instead of Python urllib for HTTPS vendor downloads
27f9744 fix(build): pluto_stream license file missing + resilient artifact upload
```

Kernel patch (in `board/tezuka/common/patches/linux/0d285126d.../`):
```
0007-usb-chipidea-udc-fix-isr_tr_complete_low-smp-race.patch
```

Not pushed to F5OEO/tezuka_fw:future. Local only.

## ✅ Root cause fixed: SMP use-after-free in chipidea UDC ISR

### Crash signature

Serial logger captured the full Oops (7 occurrences, same pattern):

```
udc_irq+0x4ec/0xe00
  → kfree+0x128/0x22c
    → free_to_partial_list+0x354/0x584  ← SLUB detects slab corruption
      → set_track_prepare+0x3c/0x70     ← tries to save stack trace
        → stack_trace_save → arch_stack_walk → unwind_frame  ← CRASH (NULL deref at 0x820)
```

Process: `mosquitto_pub` (PID 16549), CPU 0. Also hit `api_controller` and `iiod`.

### Root cause

File: `drivers/usb/chipidea/udc.c`, function `isr_tr_complete_low()` (line 1265).

The UDC interrupt handler iterates the endpoint queue with `list_for_each_entry_safe()`.
For each completed request, it releases `ci->lock` (via `spin_unlock(hwep->lock)`) to call
the USB request completion callback via `usb_gadget_giveback_request()`.

On SMP systems (Zynq is dual-core Cortex-A9), another CPU can take `ci->lock` during this
window and call `_ep_nuke()` (via `ep_disable` or `isr_setup_packet_handler`). This frees
requests and their TD nodes from the endpoint queue.

When the lock is re-acquired, `list_for_each_entry_safe`'s saved next pointer (`hwreqtemp`)
points to freed memory. The next iteration dereferences this dangling pointer, causing
SLUB slab corruption.

### Fix

Added a re-fetch of the safe pointer from the list head after each completion callback.
If the queue was emptied by `_ep_nuke()` on another CPU, we break out of the loop.

Patch: `0007-usb-chipidea-udc-fix-isr_tr_complete_low-smp-race.patch`

### ADI kernel status

Checked ADI Linux tree (`analogdevicesinc/linux`). Only `main` branch exists, no tags,
no stable tracking. ADI tree has 50 commits on top of upstream 6.12.0 with SUBLEVEL
cosmetically bumped to 77. **None of the upstream chipidea UDC fixes are included.**

No viable ADI version bump available. Staying on pinned commit `0d285126d15` with
our own patches.

## ✅ Done

### Root cause captured (2026-07-05)
- Continuous Python serial logger (`simple-logger.py`) caught the Oops at 02:45 UTC
- Full kernel dump in `tmp/serial-logs/nano-capture.log` (lines 9471-9584+)
- `udc_irq+0x4ec` identified as the originating function
- PANIC_ON_OOPS removed — was destroying crash evidence (commit e09ea15)

### Root cause fixed (2026-07-05)
- SMP race in `isr_tr_complete_low()`: after releasing `ci->lock` during the USB
  request callback, the `list_for_each_entry_safe` saved pointer becomes dangling
  when another CPU calls `_ep_nuke()` on the same endpoint
- Fix applied as kernel patch `0007-...` in `board/tezuka/common/patches/linux/`

### Kernel config changes
- `frag1.config`: SLUB_DEBUG_ON, DEBUG_KERNEL, DEBUG_LIST, DEBUG_SG, CMA=32
- PANIC_ON_OOPS removed
- tezuka-dev skill updated with crash forensics section documenting capture method

### Serial logger
- Python script at `tmp/serial-logs/simple-logger.py` using pyserial on `/dev/cu.usbserial-2230`
- Survives reboots (auto-reconnects)
- Logs to `tmp/serial-logs/nano-capture.log`

## ⚠️ Current device state

- **Running firmware `tezuka-e09e`** — deployed via direct dd to mtdblock3 (MSD broken after reboot)
- **PANIC_ON_OOPS removed** — kernel stays alive on Oops, dmesg preserved
- **SLUB_DEBUG_ON active** — catches slab corruption at first fault
- **maia-sdr.ko FAILS TO LOAD** — `module_layout` CRC mismatch (kernel and module built against different configs). The fragment change (adding DEBUG_KERNEL, DEBUG_LIST, etc.) changed `CONFIG_MODVERSIONS` CRC. The module in the rootfs was compiled against the old config. Need to rebuild both kernel AND module together.
- **Root cause patch applied** — `0007-` fixes the UDC SMP race. Needs CI build to confirm resolution.

## Not pushed

Branch is on origin/gretel/miyazaki only. Not pushed to F5OEO/tezuka_fw:future.

## Next session: tasks 1-3

### 1. Trigger CI build to verify fix

The kernel patch `0007-` is applied in the Buildroot patch directory. Need to trigger
a CI build from `fix/maia-kmod-crash-v2` branch. CI builds kernel + all packages
(including maia-kmod) with the same config, so the `module_layout` CRC mismatch
should be resolved.

- Push branch or trigger workflow_dispatch
- Deploy via MSD or direct dd
- Verify: run MQTT traffic over USB NCM, confirm no Oops

### 2. Restore MSD update.sh functionality

The MSD flash method broke because update.sh daemon exits/crashes or the USB gadget
gets into a bad state after crashes. When update.sh isn't running, MSD copy+eject
doesn't trigger the flash. Fix options:

- Check `/etc/init.d/S45msd` on the device — does update.sh start reliably?
- The LED stuck on `[timer]` indicates flash_indication_off wasn't called — update.sh
  may have crashed during flash
- Workaround: direct dd via SSH (already proven working)

### 3. Serial logger reliability

The current Python logger (`simple-logger.py`) works but needs to survive the logger
process being killed for health checks. Improve:

- Keep logger running continuously
- Use SSH for health checks (need to fix dropbear SSH — it was listening but sshpass
  connections timed out, possibly due to stale sessions or key exchange issues)
- Or: build health check INTO the logger script itself (periodic self-check)

## Credentials

All devices: root / analog
Nano: 192.168.2.1 (USB gadget, SSH currently unreliable — use serial)
Serial: /dev/cu.usbserial-2230, 115200 baud (tty.usbserial-2230 locked/stuck)
Logger: `tmp/serial-logs/simple-logger.py` → logs to `tmp/serial-logs/nano-capture.log`