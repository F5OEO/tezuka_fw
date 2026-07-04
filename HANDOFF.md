# Handoff — 2026-07-04

## Branch

`fix/maia-kmod-crash-v2` → `gretel/miyazaki` (origin), target `F5OEO/tezuka_fw:future`

8 commits on top of `a2d21fc`:

```
4bee22e fix(nano): memory corruption detection — SLUB_DEBUG, DEBUG_LIST, PANIC_ON_OOPS
06e5494 fix(nano): increase CMA from 16MB to 32MB for maia-sdr DMA
4cbccde ci: split kernel build cache from main output cache
d6b391f fix(ci): force kernel re-install after kernel cache hit
134f11c fix(ci): robust kernel build dir detection for caching
fa073e1 fix(ci): close unterminated string in cache summary echo
c8cb879 fix(build): use curl instead of Python urllib for HTTPS vendor downloads
27f9744 fix(build): pluto_stream license file missing + resilient artifact upload
```

## Done

### Memory corruption detection in kernel (NEW 2026-07-04)

Added to `board/tezuka/nano/kernel/fragment/frag1.config`:

```
CONFIG_SLUB_DEBUG=y
CONFIG_SLUB_DEBUG_ON=y         # redzone + poison at every alloc/free
CONFIG_SLUB_STATS=y
CONFIG_DEBUG_LIST=y            # validate linked list ops
CONFIG_DEBUG_SG=y              # validate DMA scatter-gather
CONFIG_PANIC_ON_OOPS=y         # reboot cleanly, don't limp with corrupted state
CONFIG_PANIC_TIMEOUT=10
```

**Why:** The 3 Oopses are all in the page allocator (`alloc_pages_bulk_noprof+0x260`) at different addresses — 2 non-paged, 1 NULL deref, 1 NULL exec. This is NOT simple OOM. It's **memory corruption**: a scribbled-on freelist pointer that only surfaces when a random allocation iterates the corrupted list.

SLUB_DEBUG_ON catches the *first* corrupting event (use-after-free, buffer overflow) at the exact alloc/free call site with a full backtrace, instead of letting corruption silently accumulate until a random Oops hours later.

**Not added: ramoops/pstore** — ramoops is useless on Nano because Zynq FSBL re-initializes DRAM on every reset (watchdog, power-on, or warm reboot). No data survives across reboot. Serial capture is already the correct forensic tool.

### CMA=32 for Nano
- `board/tezuka/nano/kernel/fragment/frag1.config`: 16→32 MB
- CI built, artifact deployed to Nano via MSD pluto.frm
- **Confirmed on hardware**: `CONFIG_CMA_SIZE_MBYTES=32`, `CmaTotal: 32768 kB`

### CI caching overhaul
- Split output cache (3GB, single-board only) from kernel cache (~200MB compressed, all builds)
- Kernel cache NOT gated by `single_board` — works for multi-board (12×~200MB ≈ 2.4GB fits in 10GB limit)
- Install stamps stripped on kernel cache hit → `make linux` re-installs fresh zImage/dtbs/modules
- Validated: nano build #3 (all caches hot, 5min finalise), fishball7020 build (47min full build, kernel cache saved)
- actionlint clean on all workflow changes

### Build fixes
- `Dashboard/bundle.py`: use `curl` instead of Python `urllib.request.urlretrieve` — Buildroot host Python 3.14 lacks SSL
- `package/pluto_stream/pluto-stream.mk`: removed `PLUTO_STREAM_LICENSE_FILES = LICENSE` — upstream repo lacks LICENSE, legal-info hard-fails
- CI workflow: `if: always()` on Upload artifact step — SBOM failures no longer gate artifact collection

## ⚠️ Nano still crashing — 3 Oopses observed (memory corruption pattern)

After deploying CMA=32 firmware, dmesg shows **3 kernel Oopses** from the CURRENT boot:

| # | Process | PC | Address |
|---|---------|----|---------|
| 1 | `api_controller.` (PID 3611) | `alloc_pages_bulk_noprof+0x260` | `3b349a00` (non-paged) |
| 2 | `mosquitto_pub` (PID 3610) | `alloc_pages_bulk_noprof+0x260` | `00000000` (NULL deref) |
| 3 | `iiod` (PID 27740) | `0x0` (execute from NULL) | `00000000` (execute from NULL) |

### Analysis — Memory corruption, not OOM

**Memory layout (nano.dtsi: `reg = <0x00000000 0x20000000>` = 512MB but Z7010 has 256MB):**

```
0x00000000 - 0x05FFFFFF   96 MB   kernel + userspace
0x06000000 - 0x0DFFFFFF  128 MB   maia recording (no-map, reserved)
0x0E000000 - 0x15FFFFFF  128 MB   free (kernel + CMA 32MB + userspace)
0x16000000 - 0x1603FFFF  256 KB   maia spectrometer
0x16040000 - 0x1FFFFFFF  ~159 MB  free (unreachable, only 256MB on Z7010)
```

~86MB available for kernel + userspace + 32MB CMA. Plenty of headroom for the running daemons (mosquitto, iiod, maia-httpd, api_controller).

**The crash pattern is diagnostic:**
1. Same PC (`alloc_pages_bulk_noprof+0x260`) for 2/3 Oopses — different callers
2. Different fault addresses (3b349a00, 00000000) — not a single corrupted pointer
3. iiod dereferencing NULL as code — function pointer table corrupted

This is the classic signature of **page allocator freelist corruption**: something scribbles on a freed page's freelist pointers (use-after-free), then any subsequent allocation that traverses the corrupted list crashes at a random address.

### Root cause candidates

1. **Maia-kmod rxbuffer cacheinv TOCTOU race** — CLOSED by patch 0003 (spinlock + WRITE_ONCE/smp_wmb). No remaining uncovered path in IOCTL_CACHEINV.
2. **Maia-kmod recording mmap `v7_dma_inv_range` on VM_IO** — REMOVED by patch 0003. UNPREDICTABLE per ARM ARM, was trashing L1 on device-memory pages.
3. **iiod/libiio IIO buffer mmap** — iiod crashed with execute-from-NULL. IIO buffer mmap in the kernel allocates pages with `iio_buffer_alloc`. A bug here (or in the AD9361 driver's DMA path) could corrupt page state.
4. **Hardware** — Zynq Z7010 has no ECC on DDR3. Marginal timing on a Chinese QSPI board could produce single-bit errors that manifest as memory corruption.

The fix: SLUB_DEBUG_ON will catch the *first* corrupting event with exact backtrace, distinguishing between the three candidates.

### Upstream maia-kmod status

Checked `~/src/uhd/maia-sdr-daniel` (main, c96c496a). Upstream commit `6f997c69` fixes kernel 6.4+/6.11+ API compat — same as our patch 0001. No additional fixes beyond that. Patches 0002-0004 (TOCTOU, UAF, UNPREDICTABLE cache op, vm_ops crash) are tezuka-only and not upstream.

## Not pushed

Branch is on origin/gretel/miyazaki only. Not pushed to F5OEO/tezuka_fw:future.

PR.md updated with current changeset.

## Next session: things to tackle

1. **Build and deploy** — CI build nano with the new SLUB_DEBUG fragment, deploy to hardware, stress-test with maia-httpd + iiod + api_controller running concurrently. Check dmesg for SLUB redzone hits — they'll show the exact backtrace of the first corrupting event.
2. **Multi-board CI** — after kernel cache is validated, re-run full 12-board CI matrix to populate all kernel caches.
3. **Push to upstream** — PR.md is ready, just need `gh pr create` when confident.
4. **If SLUB_DEBUG shows maia-kmod as culprit** — need to find remaining race not covered by patches 0002-0004. Upstream the fixes.
5. **If SLUB_DEBUG shows iiod/libiio or AD9361 driver** — need to patch Buildroot's libiio or the kernel IIO subsystem.
6. **If SLUB_DEBUG shows no corruption** — likely hardware (marginal DDR). Try memory stress test (`memtester`) and consider clock speed reduction.

## Credentials

All devices: root / analog
Nano: 192.168.2.1 (USB gadget, SSH dies with gadget restart → use serial)
Serial: /dev/cu.usbserial-2230, 115200 baud, tio