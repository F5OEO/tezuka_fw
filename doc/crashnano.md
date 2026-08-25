# Nano board kernel panic under RF/USB traffic — investigation log

## Cleanest reproduction yet (2026-08-22): crash inside memtester's own mlock(), fully isolated boot, zero SDR/DMA involvement

Built a diagnostic firmware variant that gates the three DMA-capable
auto-starts (`maia-sdr.ko` load in `S50maia-kmod`, `maia-httpd` start in
`S60maia-httpd`, `iiod` start in `S23udc`) behind a `dma_test_skip=1` kernel
cmdline flag, added only to a locally-modified `uEnv.txt` (not committed --
default behavior for every other boot/board is unchanged). This is the
"boot with DMA never armed" method fable recommended, addressing the gap in
the previous (retracted) module-unload test: this time `maia_sdr.ko` was
never loaded, `maia-httpd`/`iiod` never started, confirmed by their own
"Skipping ... (dma_test_skip on cmdline)" log lines, and independently
confirmed by boot-order timing on the very first attempt -- the first
crash below happened at the "Starting network:" step, which runs *before*
`S50maia-kmod` in the init sequence, so `maia_sdr.ko` hadn't even been
reached yet, regardless of the flag. (Caveat for precision: the *built-in*
`ad9361`/`cf_axi_adc`/`cf_axi_dds` drivers, compiled into the kernel
separately from the loadable `maia_sdr.ko` shim, still probe normally at
boot and leave an idle `[irq/33-maia-sdr]` thread -- but no IIO buffer was
ever enabled and no userspace client ever requested one, so no DMA
transfer was ever actually triggered.)

Two crashes captured on this isolated build:

1. **First boot**: `Kernel panic - not syncing: stack-protector: Kernel
   stack is corrupted in: get_device_system_crosststamp+0x6a0/0x6a8`
   (Comm: `run-parts`, PID 1036), during the "Starting network:" init
   step -- a hard, non-recoverable panic (stack canary corruption
   detected), not an Oops. Board self-rebooted via watchdog.
2. **Second boot, decisive**: logged in at the console, ran `memtester 400M
   3` directly (no maia_sdr, no maia-httpd, no iiod -- confirmed via
   `lsmod` showing zero loaded modules and `ps aux` showing neither
   daemon running). **memtester crashed the kernel within seconds, before
   finishing its very first `mlock()` call on the freshly-allocated 400MB
   test buffer** -- `Oops: 805`, `Unable to handle kernel paging request
   ... when write` at a fresh anonymous virtual address (`*pgd=00000000`,
   i.e. no page-table entry at all was found for a page the fault handler
   should have just been populating), inside `mmioset` <-
   `v6_clear_user_highpage_nonaliasing` <- `handle_mm_fault` <-
   `__get_user_pages` <- `populate_vma_page_range` <- `__mm_populate` <-
   `do_mlock` (Comm: `memtester` itself, PID 5646). A second Oops followed
   immediately in `klogd` (`lru_add+0x104`, `Tainted: [D]=DIE`) as the
   system was already dying. Also observed during this session's login:
   two more `BUG: Bad rss-counter state` warnings (non-fatal), same
   class as the earlier memtester-run cascade.

This is the single cleanest data point the whole investigation has
produced: a crash inside the most basic possible memory operation --
zeroing a freshly page-fault-allocated anonymous page during `mlock()` --
with no SDR/DMA/RF subsystem ever touched this boot. It does not by itself
distinguish "genuine DRAM write-timing marginality" from "an unrelated ARM
MM-subsystem kernel bug," but it conclusively rules out anything
specifically requiring the AD9361/axi-dmac/maia-sdr driver stack to be
active, since none of it was. Combined with the `0x00000122` cross-board
fingerprint and the "different subsystem every time" pattern throughout
this doc, DRAM write-timing marginality remains the best-fitting single
explanation. Next useful step: repeat this same isolated-boot memtester run
a few more times to build a sample of failure timing/patterns (does it
always fail this fast? always in `mlock`/page-fault code specifically, or
does location vary run to run?), and check whether corrupted words are
consistently low-16-bits (per the earlier fable-suggested discriminator).

**HIGHMEM-vs-broad-DRAM discriminator (advisor, 2026-08-22): fails at 64MB
too, entirely in lowmem.** The decisive 400MB crash above happened inside
`mlock()`'s HIGHMEM page-clear path (`v6_clear_user_highpage_nonaliasing`),
raising the question of whether this was a HIGHMEM/kmap-specific bug rather
than general DRAM marginality. Ran `memtester 64M 2` (well within lowmem,
`HighMem` zone starts at `0x30000000`, failures observed at physical
offsets ~0x0125_8600 and ~0x0140_d3f8, both ~19-21MB) -- **fails with the
same corruption signature**, low-16-bits-only stuck/scrambled bit pattern
(`0xffff0020 != 0xffffffdf`, `0xfffffffb != 0xfffffff7` -- high 16 bits
always correct, low 16 bits wrong), same class as the original
`0xffffffff != 0xffff0000` pattern from the first memtester run. This run
did not crash the kernel (memtester completed and printed `Done.`, unlike
the fatal 400MB run) -- corruption is real and detectable well below the
threshold that triggers a kernel oops. This rules out "HIGHMEM code path
bug" as the mechanism and rules out buffer-size/highmem as a precondition
-- confirms broad, low-16-bit-confined DRAM marginality. The low-16-bit
confinement is a meaningful clue on its own: this board's 32-bit DDR3 bus
is two `MT41K256M16` (x16) chips in parallel, one driving bits[15:0] and
one bits[31:16] -- consistently-low-half corruption points at one specific
chip/byte-lane's write or read-capture timing being marginal, not a
generic whole-bus fault. Next step per advisor: compare
`PCW_UIPARAM_DDR_FREQ_MHZ` in
`maia-hdl/projects/boards/pciesdr7010/ps7.tcl` against `fishball7010`'s (the
electrical clone template) -- if the frequency was inherited along with the
board-delay seed values (already known to be "a starting point" per this
board's own `ps7.tcl` comment, not yet re-tuned for this board's actual
trace lengths), a one-step frequency reduction is a single-variable, large
distributed margin experiment worth trying before anything more invasive.

**One more failure mode, final state of tonight's testing:** after several
more isolated `memtester 64M` passes (all consistent with the pattern
below -- different addresses each run, always low-16-bits-only corruption,
one run crashed silently before printing anything, one non-fatal Oops in
`mprotect()`'s `__pte_offset_map_lock` path mid-run that the system
survived), the board went fully unresponsive to console input with an
`rcu: INFO: rcu_preempt self-detected stall on CPU` report -- CPU 1 stuck
spinning in `_raw_spin_lock` inside `__pte_offset_map_lock` (the exact
same function as the earlier non-fatal Oops) for 240000+ jiffies with no
progress. This is a distinct failure mode from everything else captured:
not a data-corruption readback, not a crash/Oops, but the memory
corruption apparently scrambling a spinlock's own state so it never
releases, deadlocking one CPU indefinitely. No watchdog recovery observed
by the time testing was wrapped up for the night. **Board state: hung,
not mid-write to anything (SD, flash, etc.) -- safe to leave until a
plain power cycle, no urgency, no data at risk.** Whoever picks this back
up next just needs a normal power cycle; the board will come back on the
same diagnostic (`dma_test_skip=1`) image until `uEnv.txt` is redeployed
without the flag (the fresh build with the `S23udc` FunctionFS-gating fix,
already built at `output/pciesdr7010/`, was never redeployed -- do that
first to get RNDIS/SSH back before further testing).

## Decisive: manufacturer's own firmware also fails memtester (2026-08-23)

Directly answers "is this tezuka_fw's fault." Deployed the manufacturer's
own shipped SD image (`/media/hp-z2-dev/Variste/Doc/Download/PCIE_7010_SDR/
Firmware/firmware_load/SD_img/`: their own `BOOT.bin` -- own FSBL, own DDR
init, entirely unrelated to any tezuka_fw/maia-sdr build artifact --
`devicetree.dtb`, `uEnv.txt`, `uImage`, `uramdisk.image.gz`), verified
complete/uncorrupted via md5sum match against local files before and after
transfer (the earlier interrupted attempt from 2026-08-22 was superseded by
this full, verified transfer). Booted clean -- "Welcome to Pluto", their
own 5.15.0 kernel (vs our 6.12.77), same `192.168.2.1` USB network
convention. Their firmware ships without `memtester`, so cross-compiled a
fully **statically linked** `memtester` (from this repo's own
`memtester-4.7.1` source tree, `arm-linux-gcc -static`, verified zero
dynamic dependencies) and `scp`'d it directly into `/tmp` (their firmware
doesn't persist-mount the SD boot partition into Linux at all, so the
usual `/boot` drop-in approach doesn't apply -- `/tmp` over SSH works
regardless).

**`memtester 64M 3` on the manufacturer's own firmware fails immediately**,
same signature as everything in this whole investigation: `FAILURE:
possible bad address line at offset 0x00833658` (Stuck Address test),
then dozens more failures across Block Sequential (`0x13131212 !=
0x13131313`), Walking Ones (`0x0000dfff != 0x00002000`, `0xffff2000 !=
0xffffdfff`), Checkerboard, Bit Spread, Bit Flip, and Walking
Zeroes/Ones again -- **every single failing word has the high 16 bits
correct and only the low 16 bits wrong**, the identical lane-0 signature
seen throughout this entire investigation. **Ran all 3 full loops to
completion ("Done.") without ever crashing the kernel** -- 65 total
`FAILURE` lines logged, board stayed fully responsive the whole time.
This is a genuine difference from tezuka_fw (which typically Oops'd or
panicked within the same timeframe) -- most likely the vendor's older,
simpler 5.15.0 kernel is just less exposed to whatever secondary
kernel-level fragility (page-table/VMA corruption cascades, stack
overflows) tezuka_fw's newer 6.12.77 kernel adds on top of the same
underlying hardware fault. The raw DRAM corruption itself -- what
`memtester` actually measures -- is identical in both: same low-16-bit
confinement, same general magnitude of failures per pass.

**Conclusion: this is conclusively not a tezuka_fw configuration or
firmware defect.** The identical lane-0 (low-16-bit) DDR corruption
reproduces on completely independent firmware -- different FSBL, different
DDR init code, different kernel version, different rootfs -- with the only
constant being the physical board. Combined with the DDR retune sweep
above (3/3 candidates failed across the full known-working parameter
range), this closes the "is it our config" question definitively: it's the
physical hardware.

## Community corroboration: this matches known Zynq-7000 DDR3 hardware issues (2026-08-23)

Two AMD/Xilinx Adaptive Support forum threads describe essentially the
same failure class on the same PS DRAM hard IP, independently confirming
the direction of this investigation:

- ["Zynq 7020 DDR3L interface not reliable at full speed - Settings to
  tweak?"](https://adaptivesupport.amd.com/s/question/0D54U00005uAyXfSAK/)
  (2022): new PCB layout, DDR3L @ 533MHz/32b, `memtester` fails
  specifically on the "solid bits" pattern test; only workaround found was
  reducing DDR frequency (to 466MHz, not usable for their bandwidth
  needs); **could not reproduce via the SDK's OCM-based DRAM test, only
  with real PL-side memory traffic present** -- directly parallels this
  investigation's own finding that a raw U-Boot-level test (no PL/DMA
  traffic) doesn't reproduce it either. Root cause landed on via
  oscilloscope: DQ-idle-level anomalies and suspected **crosstalk from two
  DQ data lines onto an address line (DQ1/DQ6 -> A13)** -- a PCB
  signal-integrity defect, not a tunable parameter. An expert responder
  (`watari`) advised against adjusting `DDRIOB_DRIVE_SLEW_ADDR/DATA/_DIFF/
  _CLOCK` (register `0xF8000B5C`+) since those are PVT-compensation
  registers, not a general fix knob. Thread closed without a confirmed
  final fix.

- ["DDR3 inconsistent word errors with
  Zynq7020"](https://adaptivesupport.amd.com/s/question/0D54U00008PzEEQSA3/)
  (2024): new layout of a previously-working design, Xilinx's own "ZYNQ
  DRAM DIAGNOSTICS TEST" fails with thousands of word errors on the new
  board, passes clean on the old one. **The user explicitly tried tuning
  both DQS-to-clock delay and board delay using values calculated from
  their own board layout software -- "changing these values made no
  difference at all. We even ran this bitstream on the old working board
  and it still passed 100%."** This is the closest possible match to this
  investigation's own 3/3-failed DQS/BOARD_DELAY sweep -- independent
  confirmation from a real production case that these seed parameters are
  not a general-purpose fix for marginal DDR3 signal integrity, only a
  necessary baseline for write/read leveling to function at all (per the
  accepted answer). **What actually worked for them: physically increasing
  the termination resistor value on the address/clock lines** from the
  datasheet-suggested 40.2ohm to ~90ohm, cutting errors from tens of
  thousands to near zero -- a hardware/PCB change, not a firmware one.
  They explicitly noted they still didn't know exactly *why* it worked
  ("not sure where the problem lies since the trace width and even most of
  the lengths are the same").

**Both threads reinforce the conclusion already reached independently in
this investigation**: on Zynq-7000's hardened PS DRAM controller, DQS_TO_
CLK_DELAY/BOARD_DELAY tuning is a necessary baseline (training needs
*some* reasonable seed) but not a general remedy for marginal signal
integrity once that baseline is met -- consistent with this board's 3/3
failed sweep across the full range of values ever used in this codebase.
The two community fixes that did work (frequency reduction, physical
termination resistor changes) are both outside what's achievable in
tezuka_fw firmware alone -- the frequency option was already rejected here
for the same bandwidth reasons the 2022 poster gave, and the resistor
option requires actual board rework. If this board's fault is pursued
further, physically probing termination resistor values and/or trying a
lower DDR clock as an explicit tradeoff (accepting reduced bandwidth) are
the two concretely-precedented next moves -- not further blind delay-seed
sweeping, which both this investigation and the 2024 thread already show
doesn't move the needle.

## U-Boot-level raw memory test: does NOT reproduce the corruption (2026-08-23)

On the same physical board, still running the manufacturer's own U-Boot
(interrupted autoboot by editing the deployed `uEnv.txt`'s `bootdelay=0` to
`bootdelay=20` via manually mounting `/dev/mmcblk0p1` from the running
vendor Linux -- `fw_setenv bootdelay` alone did NOT take effect, since
`uEnv.txt`'s own `bootdelay=` line is imported and overrides the QSPI-saved
default every boot). `mtest` isn't compiled into this U-Boot build, so
built an equivalent manual test with `mw.l` (fill) + `crc32` (checksum):
filled a 24MB region (`0x600000`-`0x1dfffff`) that directly overlaps
several of the exact addresses where `memtester` found corruption under
both firmwares' Linux, then a separate 128MB region (`0x400000`-
`0x83fffff`), each with `crc32` re-checked multiple times including after
a 30s wait. **Every checksum was stable, no drift, on both regions, at
both pattern values tried (`0xaaaaaaaa`, `0x55555555`).**

This does not overturn the hardware-defect conclusion -- `memtester`
reliably reproduces the identical low-16-bit corruption within seconds on
two independent Linux/kernel stacks (tezuka_fw's 6.12.77 and the vendor's
5.15.0) on this same board, so the fault is real and physical. But it adds
a genuine, useful constraint: **the fault is not exposed by simple,
uncached, word-at-a-time physical read/write cycles the way U-Boot
performs them.** It specifically requires something about how Linux
accesses memory that U-Boot's simple loop doesn't reproduce -- most likely
cached burst-mode DDR transactions (MMU/D-cache enabled, line-fill/
writeback bursts) versus U-Boot's typically MMU-off, uncached, single-word
access pattern, though sustained access volume/duration or a specific
instruction sequence (`ldm`/`stm`, DMA-adjacent code) can't be ruled out
either. Whoever pursues the DQS/BOARD_DELAY retune further should keep
this in mind: a raw U-Boot-level "does it still fail" check would give a
false negative and is not a valid pass/fail gate for this fault -- only a
Linux-level `memtester` run (or equivalent burst-access test) actually
exercises it.

## DDR retune sweep (2026-08-23)

**Candidate A: `DQS_TO_CLK_DELAY_0 = 0.025`** (nano's proven value, down from
default 0.048). Rebuilt bitstream (Vivado) + FSBL, flashed a new `BOOT.bin`.
FSBL DDR training succeeded, board booted clean to login normally and in
isolated (`dma_test_skip=1`) mode. **Did not fix the issue.** Isolated
`memtester 64M` still showed the same low-16-bits-only corruption pattern
(`0x3f3f3e3e != 0x3f3f3f3f`, `0x55555555 != 0x5555aaaa`, etc.), and this
time crashed with a new signature: `Unhandled fault: imprecise external
abort (0x1406)` at a kernel virtual address, followed by `watchdog:
watchdog0: watchdog did not stop!` and a hard reset. An imprecise external
abort is a bus-level error reported by the memory/interconnect itself
(distinct from the page-fault-style "Unable to handle kernel paging
request" signatures seen at the default 0.048 value) -- possibly this
value pushed the DDR controller into flagging harder/more immediate errors
rather than silently returning corrupted data, but it's still the same
lane-0 corruption at bottom. Reverted to investigate candidate B next.

**Candidate B: `DQS_TO_CLK_DELAY_0 = 0.095`** (signalsdrpro's proven value,
up from default 0.048). Rebuilt bitstream + FSBL, flashed. FSBL DDR
training succeeded, clean boot to login, isolated mode confirmed. **Also
did not fix it.** Isolated `memtester 64M` again showed the same
low-16-bits-only corruption pattern (`0x42424242 != 0x42424141`,
`0xfffff7ff != 0xffff0800`) at yet more different addresses, and hit
another distinct (non-fatal this time) Oops -- a data abort in
`__dabt_svc` via the IRQ path, board survived and stayed up. Both
candidates -- 0.025 (nano's real value) and 0.095 (signalsdrpro's real
value) -- bracket essentially the whole range of DQS_TO_CLK_DELAY_0 values
that have ever actually shipped working in this codebase, and both still
show the identical lane-0 corruption signature. `BOARD_DELAY0` was left
at the common default (0.241) for both candidates -- not yet varied.

**Candidate C: `BOARD_DELAY0 = 0.202`** (signalsdrpro's proven value, down
from default 0.241), `DQS_TO_CLK_DELAY_0` reset back to the original
default 0.048 to isolate this as the only changed variable. Rebuilt,
flashed, clean boot, isolated mode confirmed. **Also did not fix it.**
Same low-16-bits-only pattern (`0x00000040 != 0x0000ffbf`, `0xffffffbf !=
0xffff0040`), yet another distinct crash signature this time -- `Fixing
recursive fault but reboot is needed!` (a fault while already handling a
fault). Board self-recovered via watchdog. **3/3 candidates failed now**,
spanning both parameters' entire known-working range in this codebase
(DQS_TO_CLK_DELAY_0 0.025/0.095, BOARD_DELAY0 0.202) plus the untouched
common default for whichever parameter wasn't the one being varied each
time.

**Assessment after 3/3 failed candidates:** since `TRAIN_DATA_EYE` is
enabled (Vivado auto-training searches around the seed at boot), failing
at every proven-working value of both tunable parameters is strong
negative evidence -- if a stable eye existed reachable from any of these
seeds, training should have found it. **This is now treated as the
investigation's practical conclusion: a genuine PCB/routing-length or
component-level defect on this specific `pciesdr7010` board, not
correctable via DDR delay seed tuning within any value this codebase has
ever actually used.** Further sweeping (finer-grained values, or
BOARD_DELAY1/DQS_TO_CLK_DELAY_1 in case lane 1's own seed is somehow
implicated despite the corruption being lane-0-confined) could still be
tried, but three failed attempts spanning the full known-working range of
both parameters is a reasonable point to stop and escalate this as a
hardware/PCB finding rather than keep iterating blind. If pursued further,
real DDR margin-measurement tooling (not available in this session) would
be needed to make further guesses better than blind.

**Verification the sweep actually tested anything (advisor review,
2026-08-23):** a distinct `BOOT.bin` md5sum per candidate only proves the
files differ, not that the DDR register writes changed -- checked
directly by comparing the Vivado-generated `ps7_init.html`'s "Board delay
[0]" value between the original committed `.xsa` (git `edd188f`, extracted
via `git show`: `0.241`, matching the untouched default) and candidate C's
freshly generated one (`0.202`, matching the intended change). Confirms
`ad_ip_parameter` genuinely propagated into the PS7 DDR init code, not a
silent no-op. Candidates A/B's own build artifacts were already
overwritten (each FSBL rebuild starts from `rm -rf build/sdk`) so they
can't be individually re-verified the same way, but since the identical
script mechanism (`create_fsbl_project.tcl` + the same `ad_ip_parameter`
calls) was used for all three, this is strong indirect evidence the whole
sweep was valid rather than three tests of one unchanged configuration.

**Correction: crash-signature variety is not independent evidence
(advisor review).** Earlier phrasing in this doc listed "imprecise
external abort," "IRQ data abort," "recursive fault" as if each added
weight to the conclusion. They don't -- a wild write to a random physical
address produces whatever fault the victim page/struct happens to trigger;
one underlying mechanism, different dice rolls each time. **The actual
evidence is the invariant that held across every single failure in this
whole sweep and the sessions before it: the high 16 bits of every
corrupted word are always correct, only the low 16 bits are ever wrong,
and the failing address moves between runs.** That's the finding; the
specific fault type each time is incidental.

**Open question, not resolved: the nano connection.** This investigation
began on the `nano` board months before `pciesdr7010` existed, and the
strongest cross-board evidence gathered earlier (`0x00000122` recurring at
the same struct offset on both boards) was the reason DRAM timing became
the leading hypothesis in the first place. A PCB routing/component defect
specific to this one `pciesdr7010` card cannot explain nano's original
crashes -- those are a different board, different PCB, different DDR
routing entirely. Either the `0x00000122` cross-board match was
coincidental (a plausible fixed value recurring for unrelated reasons on
two different boards is not impossible, but was treated as strong evidence
at the time), or "defect on this specific board" is too narrow a
conclusion and this is actually a design-level DDR margin issue shared
across the whole board family (all of which, per the earlier sweep table,
default to the same never-individually-tuned 0.048/0.050/0.241/0.240
values copied board-to-board). This doc does not currently have enough
evidence to say which. Whoever picks this up next should treat "hardware
defect on pciesdr7010" as the demonstrated conclusion for *this session's
board*, but should not assume it explains nano's history without
separately re-running an isolated `memtester`-style test on nano itself.

## Conclusion (2026-08-22): lane-0 DDR write/read-capture timing marginality -- hardware finding, not a firmware bug

Ran `memtester 64M 2` three times in a row on the fully isolated boot
(no maia_sdr, no maia-httpd, no iiod). Results:

- Run 1: failures at offset 0x0125_85fc and 0x0140_d3f8.
- Run 2: crashed/hung before printing any result -- no panic message at
  all, straight to a silent watchdog reboot. Itself informative: even a
  64MB test doesn't reliably survive to completion.
- Run 3: failures at offsets 0x0349_3278 (stuck-address test),
  0x0117_b634, 0x008c_787c, 0x0112_8854, 0x0150_70d8 (several different
  test phases).

**Every single failing word across all three runs has the high 16 bits
correct and only the low 16 bits wrong** (`0x20201f1f != 0x20202020`,
`0x5555aaaa != 0x55555555`, `0xaaaa5555 != 0xaaaaaaaa`, etc. -- and the
original two runs' `0xffff0020 != 0xffffffdf` pattern). **And the failing
addresses are different in every run.** Per advisor's discriminator: a bad
cell or a cold solder joint fails at the *same* address every time;
addresses moving between runs while consistently landing on the same
16-bit half is the signature of write-leveling/DQS-to-CLK timing
marginality on one specific lane -- this board's 32-bit DDR3 bus is two
`MT41K256M16` (x16) chips, and this is lane 0 (bits[15:0]) specifically,
matching exactly the `_0`-suffixed seed parameters in `ps7.tcl`
(`PCW_UIPARAM_DDR_DQS_TO_CLK_DELAY_0 0.048`, `PCW_UIPARAM_DDR_BOARD_DELAY0
0.241`). Those values are nearly identical to the `_1` (lane 1) values
(0.050 / 0.240) -- consistent with having been copied from fishball7010, a
board whose two DRAM chips were likely routed symmetrically, onto a
different (PCIe card) form factor whose own file comment already flags
"different DDR trace lengths ... may need re-tuning." A seed that's off for
one lane's real trace delay can make Vivado's `TRAIN_DATA_EYE`/
`TRAIN_WRITE_LEVEL` auto-training converge on a marginal point for that
lane specifically, rather than failing outright -- which is exactly this
symptom (works most of the time, corrupts unpredictably under load,
crashes on the most basic possible memory operations when it doesn't).

**This supersedes the earlier maia-kmod TOCTOU race (patch 0004) and the
DMA-descriptor hypotheses as the root cause of every crash captured in
this investigation, on both boards, across the whole multi-month
timeline.** 0004 remains in the tree as a real, independently-justified fix
(a genuine bug, just not this one) -- no reason to revert it.

**Deliberately not attempted tonight: retuning `DQS_TO_CLK_DELAY_0`/
`BOARD_DELAY0` and reflashing.** This requires a Vivado FSBL rebuild and a
new `BOOT.bin`, and per advisor: a bad `BOOT.bin` is a one-way door on this
board tonight -- boot mode is a physical DIP switch, BootROM only reads the
one file from the SD's FAT partition, and recovering from a bad flash needs
hands physically on the SD card (as tonight's earlier recovery did). With
the user asleep and unavailable to help recover a bricked boot, and no
validated JTAG recovery path for this specific board (the toolkit in
`tools/jtag-recovery/` is nano-specific -- wrong FSBL, wrong DDR config),
this is not a safe unattended action even under a broad standing
authorization to proceed without asking -- it's a different risk class from
everything else attempted tonight (all of which was recoverable via SD
reformat or a plain reboot). **Left as the clear, well-evidenced next step
for whenever the user is available:** sweep `DQS_TO_CLK_DELAY_0`/
`BOARD_DELAY0` (and possibly `_1` in parallel, in case symmetric routing
assumptions are wrong in both directions) across a small range of values
around the current 0.048/0.241, rebuilding FSBL and testing `memtester`
stability at each step, ideally with a second, known-recoverable board or
a validated JTAG procedure on hand before the first flash.

**Bottom line for the report: this looks like a genuine hardware/PCB
finding (DDR lane-0 trace-length-driven timing marginality on the
`pciesdr7010` board specifically), not a fixable firmware bug.** A DQS/
board-delay retune is a plausible *mitigation* if it converges training
onto a stable point for this board's actual routing; it would not be a
"fix" for a genuinely marginal lane -- and if the retune sweep doesn't find
a stable value, that's a real hardware/PCB conclusion (routing length
mismatch on this specific card) worth escalating as such, not something to
keep chasing in software.

**Known side effect of this diagnostic build, not yet reverted:** skipping
`iiod` in `S23udc` broke the whole USB composite gadget's UDC bind
(`failed to start composite_gadget: -19`), because the FunctionFS function
needs `iiod` to open/activate its endpoints before the gadget can bind --
this took the RNDIS network interface down too, not just IIO-over-USB.
The board's physical Ethernet (`eth0`, DHCP) still works and was used for
console-based access instead. If further isolated-boot testing is wanted
without losing USB networking, `S23udc`'s iiod skip needs to also skip
(or stub) the FunctionFS gadget function it's paired with, not just the
daemon launch.


## SD reformat clears the "board won't boot" scare, and produces a fresh reproduction with new evidence (2026-08-22)

The vendor-image test attempt (below) left the board unresponsive after a
crash mid-SD-DMA. Rather than risk JTAG recovery with the wrong board's FSBL
(the toolkit in `tools/jtag-recovery/` is nano-specific), the SD card was
pulled, its FAT partition reformatted, and the known-good tezuka_fw boot
files (`output/pciesdr7010/images/sdimg/`: `BOOT.bin`, `devicetree.dtb`,
`system_top.bin`, `uEnv.txt`, `uImage`, `uramdisk.image.xz`) copied back on
fresh. `BOOT.bin` was confirmed byte-size-identical (1949416 bytes) to what
was already on the board before it went unresponsive, so the FSBL/bitstream
were never actually in question -- pulling the card to do the reformat most
likely also finally delivered a genuine power-cycle (the board is PCIe
+12V-powered; a USB-cable disconnect/reconnect, which is all that had
happened up to that point, never touches this rail). Board booted clean
immediately after: full daemon stack up, SSH reachable, ran ~2 minutes.

**Then it reproduced again, on the fresh card, ~2 minutes after login** --
conclusively separating this bug from the SD/FAT corruption and BOOT.bin
concerns above; those were real but unrelated to the underlying crash class.
Three Oops in sequence: (1) `Oops: 807` writing to fault address
`f11aad54`, above the stack region's own end (`Stack: (0xf11a9ea8 to
0xf11aa000)`, `*pte=00000000`) while in `do_epoll_wait` (Comm: mosquitto) --
same VMAP_STACK-guard-page-write family as the `__und_svc+0x2c`/sp+0x11c
hits from the memtester run below, not a new signature. (2) and (3) both
`Oops - undefined instruction: 0` at the *identical* PC,
`__pte_offset_map_lock+0x88`, called from `filemap_map_pages` <-
`handle_mm_fault` <- `do_page_fault` (killing `api_controller.` then `awk`
in turn) -- **initially misread as kernel-text corruption** (the `Code:`
bytes decode as a coprocessor opcode, `ee1d0003`), but the two `Code:`
dumps are byte-identical across two different CPUs and two different
processes; random corruption cannot reproduce the same garbage twice, so
`ee1d0003` is simply the genuine compiled instruction at that offset (ARM
kernel text legitimately uses `mrc`/`mcr` for per-CPU/TPIDRPRW access) --
retracted, this is not evidence of anything beyond "the same fault site hit
twice." After the third Oops the board self-rebooted via the Xilinx
watchdog (`cdns-wdt`, 10s timeout, confirmed via `dmesg`) -- consistent with
the memtester-run auto-recovery noted below, and now observed twice.
**Practical implication: this class of crash, once it hits non-fatal Oops
rather than a full "Bad mode" hard hang, does not need physical/JTAG
recovery -- the watchdog clears it in ~10s on its own.**

**Open question raised by advisor review (2026-08-22): DRAM-timing vs.
rogue-DMA-master are NOT yet distinguished.** The only real DRAM-specific
evidence so far is memtester failing on its own freshly-mlocked userspace
pages -- but that's simply physical RAM being corrupted, which a
misprogrammed `axi-dmac` descriptor (this repo's own doc already records a
prior "DMA bus-width mismatch vs vendor firmware" finding) would produce
identically, including a repeated fixed value like `0x00000122` (arguably
*more* naturally explained as a repeated descriptor/stream field than as
analog DDR timing marginality, which wouldn't reproduce an exact constant).
Decisive, zero-rebuild test: stop every DMA producer (`pkill maia-httpd;
rmmod maia_sdr`, ensure no `iiod`/streaming), then run `memtester 400M 3`
again. Still fails the same way -> DRAM write-capture confirmed, proceed to
`ps7.tcl` DQS/board-delay retuning. Passes clean -> DRAM hypothesis
falsified, redirect to `axi-dmac` descriptor programming. Also check
whether corrupted words are consistently in the **low** 16 bits (as seen so
far: `0xffff0000`, `0x0b0b0a0a`) -- consistent low-half-word corruption
points to a real DRAM byte-lane; full-32-bit smearing points to DMA
overwrite. The vendor-image test is a poor discriminator here (changes
bitstream+kernel+DMA config all at once) -- hold off on it until this
module-unload test has been run.

## Decisive finding (2026-08-22): crash reproduces with maia_sdr fully unloaded -- rules out rogue-DMA-master

Running the module-unload discriminator test advisor proposed (kill
`maia-httpd`, kill `iiod`, `rmmod maia_sdr`, then `memtester` in isolation).
Two crashes happened before memtester could even be started:

1. With `maia-httpd` killed but `maia_sdr` still loaded (module refcount had
   already dropped to 0, `iiod` still running): a fatal-looking `Oops: 5`
   NULL-pointer deref (`when read`, address `0x14`) in `__slab_free`, called
   from `kmem_cache_free` <- `unlink_anon_vmas` <- `free_pgtables` <-
   `exit_mmap` <- `mmput` <- `begin_new_exec` (Comm: `api_controller.`,
   re-exec'ing itself). Non-fatal -- board self-rebooted via watchdog as
   before.
2. After `iiod` was also killed and `rmmod maia_sdr` actually **succeeded**
   (confirmed by the next crash's own header: `Modules linked in: [last
   unloaded: maia_sdr(O)]`): another crash, `Oops: 805` write fault in
   `do_vmi_align_munmap` <- `vms_gather_munmap_vmas` <- `do_vmi_munmap` <-
   `__vm_munmap` <- `elf_load` <- `load_elf_binary` (Comm: `sed`, another
   process exec()ing). Board did NOT auto-recover this time (unlike the
   previous three Oops in this same session) -- USB fully disconnected on
   the host side at 01:21:22, no watchdog recovery after 2+ minutes (prior
   recoveries were all within ~10-15s), needs a physical power cycle.

   **Correction (fable second-opinion review, 2026-08-22): the "rules out
   rogue DMA" conclusion originally written here was overstated -- retracted
   and replaced.** `maia_sdr.ko`'s own source has no dmaengine/axi-dmac
   client code, no IRQ handler, no workqueue at all -- it is a passive
   cdev/mmap shim over a reserved-memory carve-out
   (`board/tezuka/pciesdr7010/dts/fishball.dtsi`: `maia_sdr_spectrometer`
   0x16000000-0x16040000, `maia_sdr_recording` 0x6000000-0x16000000). The
   actual DMA engine driver is `axi-dmac`, and `CONFIG_AXI_DMAC=y` in the
   real build config (`output/pciesdr7010/build/linux-custom/.config`) --
   **built directly into the kernel image, not a loadable module** --
   so `rmmod maia_sdr` does not remove it from the picture at all. What the
   test actually confirmed: no known userspace client (`maia-httpd`,
   `iiod`) was running, and `maia_sdr.ko`'s own code was gone -- it does
   NOT confirm no axi-dmac hardware channel was still armed/mid-transfer,
   since nothing checked IIO buffer-enable state
   (`/sys/bus/iio/devices/iio:deviceX/buffer/enable`) before the crash, and
   a process stuck in D-state deep in the DMA-buffer-disable path (SIGKILL
   cannot interrupt uninterruptible sleep) would be invisible to a plain
   `ps` check -- the `rmmod` EBUSY seen just before this (refcount 4 with
   `maia-httpd` already killed) is consistent with exactly that, not with
   `maia_sdr.ko` itself holding a reference (its only ref-takers are cdev
   `open()`s, confirmed by reading the source). Cleaner test for next time:
   boot with `maia_sdr`/`maia-httpd` never started at all (`rdinit=/bin/sh`
   from U-Boot, or disable the init script) so DMA is never armed to begin
   with, rather than trying to quiesce it after the fact -- verify no
   `nowayout` watchdog dependency first, or a bare shell with nothing
   petting the watchdog will produce spurious resets that look like more
   crashes.

Both of the last two crashes are in the same general family as each other
(exec()-time VMA/address-space teardown: `unlink_anon_vmas`/`free_pgtables`
and `do_vmi_align_munmap`, both walking/freeing a process's VMA tree) and
distinct from the three signatures before them (epoll_wait wild write,
page-fault-path `__pte_offset_map_lock` x2) -- five distinct kernel
subsystems hit across five crashes on this one card in about 15 minutes,
continuing the "different subsystem every time" wild-write signature that's
been the throughline of this whole investigation. The DMA-driver variable
was NOT actually eliminated here (see correction above -- `axi-dmac` is
built into the kernel and was never removable by this test); a real, valid
`memtester`-in-isolation run with DMA genuinely never armed (per the
cleaner boot-time-disable method above) remains the test that actually
distinguishes DRAM-timing from DMA-descriptor, and still hasn't been run.

**Second observation worth acting on (fable review):** this is 3-for-3
reproductions in ~15 minutes this session, versus a much more intermittent
historical hit rate (per `project_nano_crash_investigation.md`, prior runs
took anywhere from seconds to 50+ minutes to reproduce). A hazard rate that
just got much higher suggests something recently changed rather than pure
constant-probability hardware marginality -- worth checking against the
last known-good bitstream/kernel, given the working tree currently has
three `system_top.xsa` files and an untracked `maia-sdr copy.c`/local patch
set modified relative to git HEAD.

## Candidate root cause (2026-08-22): DDR byte-lane write-capture failure

**Status: candidate, not confirmed.** This supersedes (in priority, not by
deleting) everything below it in this doc as the leading hypothesis, because
it is the first explanation that accounts for every distinct crash signature
seen on both boards with a single mechanism, rather than one narrow slice of
them.

### 0x00000122 reappeared (2026-08-22, during the vendor-firmware test attempt)

While attempting the decisive vendor-image test below, transferring the
manufacturer's own SD boot files and then running `md5sum` on the freshly
copied `uramdisk.image.gz` to verify the transfer, the board crashed again
-- this time with a raw stack dump containing the exact value
**`0x00000122`** (`db40: 00000122 a9fd3461 00000000 a9fd3461 ...`), the same
fixed corruption fingerprint documented as the nano investigation's original
headline clue (recurring at varying physical addresses across independent
crashes, on nano, months before pciesdr7010 existed). This crash was in
`dma_cache_maint_page` (`Comm: md5sum`, `Unable to handle kernel paging
request ... when read`), a cache-coherency-maintenance function called
around DMA transfers -- consistent with the SD-card DMA crash immediately
preceding it (`dma_map_sg_attrs` / `sdhci_pre_dma_transfer` /
`mmc_blk_mq_issue_rq`, also `Oops`, also during file I/O off the SD card).
Between these two crashes and the memtester failures above, five distinct
kernel subsystems were hit on pciesdr7010 in a single session (data abort /
NULL deref in `nd_jump_root` / prefetch abort / undefined-instruction stack
overflow x4 / MMC block-I/O DMA / `dma_cache_maint_page` with the 0x122
fingerprint) -- this is the strongest cross-board correlation the
investigation has produced: the same fixed corruption value, on two
different boards, two different HDL implementations, months apart,
clustering specifically around DMA/cache-maintenance and heavy memory
I/O. This is no longer "the concurrent-mmap-race lead" or "the USB-gadget
boot-time lead" -- treat those as superseded pending the vendor-image test.

**Board state after this crash: unresponsive, did not auto-recover** (unlike
the earlier memtester crash cascade, which self-rebooted via watchdog after
several non-fatal Oops). SSH dead, serial console stalled with no further
output past the last trace -- likely the SD block-I/O queue wedged
permanently after the DMA corruption. Needs a physical power cycle to
recover; not something recoverable over SSH/serial alone. The vendor-image
deployment (BOOT.bin/devicetree.dtb/uEnv.txt/uImage) was in progress and
incomplete when this happened -- only `uramdisk.image.gz` had actually
landed on `/boot` before the connection dropped; the other 4 files still
show tezuka_fw's own (unchanged, verified via matching size) versions, so
the board was NOT left with a broken/partial `BOOT.bin` -- it's safe to
power-cycle and it should come back up on tezuka_fw's own firmware as
before. The vendor-image decisive test itself has not yet actually
completed -- this needs to be retried after recovery.

### Evidence

On pciesdr7010, current build (`0004` + fresh kernel/rootfs applied, see
below), ran `memtester 400M 3` directly on target (`/usr/bin/memtester`,
already installed). Two things happened in the same run:

1. **memtester itself failed**, not with isolated single-bit errors but long
   contiguous runs where the low 16 bits of each 32-bit word stayed stuck at
   *stale* data while the high 16 bits updated correctly:
   ```
   Solid Bits          : FAILURE: 0xffffffff != 0xffff0000 at offset 0x055ab094.
                          FAILURE: 0x00000000 != 0x0000ffff at offset 0x055ab098.
                          FAILURE: 0xffffffff != 0xffff0000 at offset 0x055ab09c.
                          ... (continues for many consecutive 4-byte offsets)
   Block Sequential    : FAILURE: 0x0b0b0b0b != 0x0b0b0a0a at offset 0x0c3b5ff4.
                          FAILURE: 0x0b0b0b0b != 0x0b0b0a0a at offset 0x0c3b5ff8.
                          ... (continues for many consecutive 4-byte offsets)
   ```
   In the Block Sequential case the low half-word is literally the *previous
   block's* value (block 0x0a instead of the just-written 0x0b) — a write to
   the low 16 bits silently not landing. This pattern (long runs of a stuck
   half-word, upper lane correct) is a data-path/write-timing symptom, not
   something a software wild-write can produce — a kernel bug corrupting its
   own memory cannot explain memtester failing on its own freshly-mlocked,
   kernel-uninvolved userspace pages.

2. **The kernel crashed during the same run**, four back-to-back
   non-fatal Oops instances (`Internal error: Oops: 807 [#1]` through
   `[#4]`) each reading `Unable to handle kernel paging request at virtual
   address <addr> when write`, followed shortly after by a `Bad mode in
   prefetch abort handler` and a cascade of `BUG: Bad rss-counter state`
   across many unrelated `mm` structs. Every instance has `PC is at
   __und_svc+0x2c/0x5c` — an undefined-instruction trap, meaning control
   flow had already jumped somewhere invalid *before* this point — and the
   faulting write address is, in every instance, exactly the current
   process's own kernel stack overflowing by a fixed offset: e.g. fault at
   `0xf559a07c` with `sp: f5599f60` — the delta is `0x11c` (284 bytes) —
   matching the `Code:` line's final instruction `(e58dc11c)` = `str r12,
   [r13, #284]`, i.e. `svc_entry`'s exception-frame push landing past the
   8KB `VMAP_STACK` guard region. This is a *secondary* symptom of already-
   corrupted control flow, not an independent bug — don't chase it as one.

   3 of the 4 instances are `Comm: api_controller.` (this repo's own
   `overlay_tezuka/root/api_controller.sh`-spawned MQTT handler), matching
   the process implicated in the earlier `nd_jump_root` crash captured
   tonight (see "Cross-board confirmation" below). **This is exposure, not
   causation** — `api_controller.sh` forks a new process per MQTT message,
   so it has far more syscall-entry events per second than anything else on
   the system, making it statistically the most likely victim to be caught
   mid-syscall when memory gets corrupted. `klogd` shows up for the same
   reason. Do not write this up as "api_controller.sh is the source" —
   that's a dead end for the next person to chase.

### Why this explains everything, not just this crash

A byte-lane/DQS write-timing defect that silently drops writes to specific
half-words under memory pressure would corrupt: return addresses on a stack
(→ the earlier `PC=0, LR=nd_jump_root` NULL-pointer crash), code targets
jumped to via a corrupted pointer (→ undefined instruction → `__und_svc` →
this crash), `mm` accounting structures (→ the rss-counter cascade), and
would produce a *repeating fixed value at varying physical addresses* — nano
investigation's original headline clue (`0x00000122`), which never had a
satisfying explanation under the concurrent-access-race hypothesis. One
mechanism, every signature, both boards.

### The decisive test (do this before touching any DDR delay values)

`maia-hdl/projects/boards/pciesdr7010/ps7.tcl`'s `DDR_BOARD_DELAY0/1` and
`DQS_TO_CLK_DELAY_0/1` (one pair per 16-bit byte lane on this 32-bit bus)
were copied from `fishball7010` as a starting point, with an existing
comment in that file already flagging different trace lengths on this
board. These were only verified "byte-identical to the manufacturer's own
shipped firmware for this board" earlier in the investigation — which
proves the manufacturer made the same assumption, not that the values are
electrically correct. But `TRAIN_WRITE_LEVEL`/`TRAIN_READ_GATE`/
`TRAIN_DATA_EYE` are all enabled, meaning these are just seed values for
runtime training, not fixed absolute delays — so hand-editing them without
a measurement is guesswork.

Instead: **boot the manufacturer's own SD image** (already have it, it's
where the FSBL for the byte-identical comparison came from) and run
`memtester` on it.
- Vendor image fails the same way → genuine board-level DDR marginality
  present in shipped firmware, not introduced by tezuka_fw. This would
  explain "not the only board to have this."
- Vendor image passes clean → something in tezuka_fw's config/clocking
  differs from the vendor's despite matching init tables, and that delta is
  the actual bug.

Cheaper fallback if the vendor image isn't readily bootable: run
`memtester` on `fishball7010` (the board these delay values were copied
from). Pass there + fail on pciesdr7010 → board-specific marginality with
the reused values as prime suspect.

## 0004 falsified as the cause (2026-08-21) — real bug, wrong crash

Before flash-testing `0004` (below), checked whether its precondition — two
concurrent `mmap()` calls on `/dev/maia_sdr_rxbuffer` — can actually occur at
the moment the pciesdr7010 crashes happened. It can't: `grep`ing
`maia-httpd`'s Rust source (`maia-httpd/src/rxbuffer.rs`,
`maia-httpd/src/fpga.rs`) turns up exactly **one** call site for
`RxBuffer::new()`/`Dma::new()` in the entire codebase, invoked once at
process startup (`Fpga` construction) and never again — no per-client,
per-request, or per-connection remapping. Both captured crashes hit at
`maia-httpd`'s very first, only mmap of the boot, with no second process
ever attempting to map that device at that moment. Without a second racing
caller, the TOCTOU window `0004` closes cannot be what fired.

**0004 is being kept anyway** — it's a real bug (a `maia-httpd` crash/respawn
racing its own re-mmap against the dying process's `vm_close`, or any future
second consumer, would still hit it) — but it should not be read as a fix
for this crash, and a clean boot after deploying it would not be meaningful
evidence either way. Falsified by reading the client code before spending a
flash-and-reboot cycle on it, which is the cheap way to learn this.

**What this leaves:** the two-concurrent-iiod-context hypothesis this doc
names below was also premised on two racing mmap-style callers; it's not
directly killed by this finding (iiod's contexts are a different code path,
the generic IIO buffer layer, not `/dev/maia_sdr_rxbuffer`), but the
pciesdr7010 crashes specifically did not involve iiod client activity either
— they hit during the daemon-stack-up phase of boot, before any interactive
client of any kind connects. The more promising next move is not another
static read but the reproduction itself: pciesdr7010 hit the exact same
crash signature 2-for-2 at the identical boot instant (right as the init
sequence reaches `Welcome to Tezuka`), which is a far tighter, cheaper
bisection target than nano's "seconds to 50+ minutes" ever was. Candidate
first cut, from the boot log (lines 698-706): skip `Starting UDC Gadgets`
(USB composite gadget: mass storage LUN, RNDIS, `ttyGS0` console) via
`rdinit=/bin/sh` and bring up the rest of boot manually, since that's the
step running concurrently with the process (`api_controller.sh`) whose stack
was corrupted in crash #2. If clean, skip the RF switch step next
(`rfinput`/`rfoutput`, unconditional every boot, already nano's boot-time
suspect below).

## Leading fix candidate (2026-08-21): maia-kmod rxbuffer mmap TOCTOU

`package/maia-kmod/` already carries two prior attempts at this exact bug:

- `0002-fix-close-reset-vm_start-and-guard-ioctl.patch` (commit `07b22d9`):
  reset `vm_start` on `munmap()` and reject `IOCTL_CACHEINV` when nothing is
  mapped, to stop a stale virtual address reaching
  `v7_dma_inv_range()`/`arm_cache_outer_inv_range()`.
- `0003-fix-rxbuffer-mmap-cacheinv-race-with-spinlock.patch` (commit
  `5905dc4`): added a spinlock around the `mmap_done`/`vm_start`
  reads/writes in `maia_sdr_rxbuffer_mmap()`, `vm_close()`, and the cacheinv
  path.

Both are already applied in the build the pciesdr7010 crashes above were
captured on — so the bug reproduced *despite* both fixes already being in
place. Rereading 0003 explains why: it has to **release the spinlock around
`remap_pfn_range()`** in `maia_sdr_rxbuffer_mmap()`, because
`remap_pfn_range()` can sleep (GFP_KERNEL page-table allocation) and a
spinlock can't be held across a sleeping call. That reopens the exact race
0003 set out to close, at its most consequential point: two concurrent
`mmap()` calls on `/dev/maia_sdr_rxbuffer` (e.g. from two simultaneous
`iiod` client contexts — one over the USB FunctionFS backend, one over the
network/RNDIS backend, the "concurrent multi-context access" lead this doc
names below as untested) can both observe `mmap_done == 0` before either
sets it, both `remap_pfn_range()` the same physical DMA buffer into two
different processes' address spaces, and then both write
`mmap_done`/`vm_start` — whichever finishes last silently wins, leaving
`vm_start` pointing at the wrong process's address for the other mapping.
Any subsequent `IOCTL_CACHEINV` on the "losing" mapping then invalidates L1
cache lines (discard, not writeback) at a stale/wrong virtual address —
silently dropping live data rather than writing anything recognizable,
which fits the "wild write into general RAM" signature (see "Key evidence"
below) better than a raw pointer write would: a repeating fixed value at
varying physical addresses is exactly what cache-line-granularity
corruption looks like.

Fix written and cross-compiled clean against the pinned kernel (6.12.77,
`output/pciesdr7010/build/linux-custom`, using
`output/pciesdr7010/host/bin/arm-linux-gcc`):
`package/maia-kmod/0004-fix-rxbuffer-mmap-toctou-with-mutex.patch`. Replaces
the spinlock with a mutex and holds it across the entire
check-then-remap-then-set sequence in `maia_sdr_rxbuffer_mmap()` (safe since
`.mmap`/`.close` only ever run in sleepable process context), closing the
window 0003 had to leave open. `arm_cache_outer_inv_range()` was also moved
outside the lock in this revision — it only depends on `mem_base_addr`
(fixed at probe time), not `vm_start`/`mmap_done`, so it was never actually
protected by the lock, only positioned inside it.

Lock-ordering was checked explicitly before trusting this: `.mmap` and
`vm_ops->close` both run under `mm->mmap_lock` (held by core mm), so both
of this patch's lock acquisitions nest inside `mmap_lock`. The only other
acquirer, `IOCTL_CACHEINV`, holds no mm lock on entry — the risk was
whether anything it calls under `drvdata->lock` could reach back into mm
code (an ABBA deadlock). Checked both cache-maintenance calls directly:
`v7_dma_inv_range()` (`arch/arm/mm/cache-v7.S`) is a straight-line loop of
`mcr p15` cache-by-MVA instructions, no page-table walk; `arm_cache_outer_inv_range()`
(`arch/arm/mm/outercache.c`) dispatches to the PL310 L2 controller over
MMIO on a physical address. Neither touches `mm_struct`/`mmap_lock`, so the
ordering is one-directional and safe.

**Not yet flash-tested on hardware** — this is a static-analysis-driven fix,
verified for correctness and cross-compiled clean, but not a confirmed root
cause; the crash could still have another contributing source. Next step
once tested: rebuild `board-fpga`/kernel modules for pciesdr7010 (or nano)
with this patch applied and re-run the same concurrent-load reproduction
recipe documented below — the boot/login-prompt transition on pciesdr7010
reproduced 2 for 2 on the very first session, a much tighter loop than
nano's "seconds to 50 minutes."

## Cross-board confirmation (2026-08-21, pciesdr7010)

The same corruption class reproduced on `pciesdr7010` (new board, different
physical FPGA implementation, brand-new bring-up) during its first serial
console session, **twice**, on two separate boot cycles, at the exact same
point in boot both times: immediately after the last init script line
(`Starting maia-httpd`) prints `Welcome to Tezuka` and the login prompt
appears — i.e. right as the full daemon stack (mosquitto, maia-httpd,
dropbear, crond, chronyd, USB composite gadget with mass-storage LUN,
Ethernet/macb, RF `rfinput`/`rfoutput` switching) finishes coming up, before
any interactive login. Not "idle for 10-15 minutes" as first reported to the
user in-session — the two captured occurrences are tightly clustered at the
boot/login-prompt transition, matching the "fires at boot before deliberate
stress, driven by the RF-switch/full-daemon-stack init sequence that runs on
every boot" pattern this doc already describes for nano below, not a
long-idle timeout.

Two different crash signatures were captured back-to-back in the same serial
log (`/tmp/pciesdr7010_console2.log`):

1. `Bad mode in data abort handler detected` / `Internal error: Oops - bad
   mode: 0` — no register dump (the abort mode itself is corrupted, so the
   kernel can't produce one). Same signature nano hits.
2. `Internal error: Oops: 80000005` (NULL pointer dereference), fully
   decoded: `PC is at 0x0`, `LR is at nd_jump_root+0xc8/0x10c`, faulting
   process `Comm: api_controller.` (this repo's own
   `overlay_tezuka/root/api_controller.sh`-spawned process, PID 7596). r0/r5/
   r6/r8 all point into "2-page vmalloc region starting at 0xf2ae0000
   allocated at kernel_clone+0xa0/0x334" — i.e. that process's own kernel
   stack — while r7 and r10 read back as flat NULL and the call trace itself
   is broken (`nd_jump_root from 0xef`, `Code: bad PC value`). This is a
   **new crash signature**, not previously seen on nano, and the first one in
   either investigation to land on a userspace init script's kernel stack
   rather than an internal kernel subsystem's data structure.

This is useful evidence on two fronts: it reproduces across two independent
HDL implementations sharing the same kernel/driver stack (reinforcing this is
a software/driver bug, not board-specific hardware or a bad DDR config on
either board — pciesdr7010's DDR/PLL/MIO init tables were separately verified
byte-identical against the manufacturer's own shipped firmware, see
`doc/templateboard.md` / project memory), and it adds a fourth distinct
corruption-surfacing-site to the "wild write into general RAM, not a local
logic bug" evidence already gathered on nano (see "Key evidence" below) —
consistent with, not yet narrowing, the existing concurrent-access-race
hypothesis (IIO buffer DMA layer / USB gadget composite driver) — a corrupted
process kernel stack is exactly the kind of victim a wild write anywhere in
that address range would produce, but does not by itself implicate
`api_controller.sh` as the source.


## Symptom

Zynq-7010 "nano" board (PlutoSDR-family) panics/oopses under RF sample streaming
and/or USB traffic. Crash signature is **not consistent** — it manifests in
different, unrelated kernel subsystems each time (page allocator, SLUB/SKB
allocation, TCP/skb trim, IRQ dispatch, LRU/page-cache list management, fork).
This pattern — corruption surfacing wherever the next unlucky code path touches
already-scribbled memory — is the signature of a **wild write into general
system RAM**, not a logic bug local to whichever function the trace happens to
point at.

## Environment

- Kernel: `analogdevicesinc/linux` @ `0d285126d15a9ea77f1c5bbfd2a4a6c40dfd648e`
  (pinned in `configs/nano_*_defconfig`), linux 6.12.77-based ADI fork.
- 512 MiB DDR (Micron MT41K256M16, confirmed via board photo + `bdinfo`/`mtest`).
- Reserved memory (`nano.dtsi`, both `no-map`):
  - `maia_sdr_recording@6000000` — 256 MiB, base 0x6000000 (96–352 MiB)
  - `maia_sdr_spectrometer@16000000` — 256 KiB, base 0x16000000 (~352 MiB)
- `CONFIG_CMA_SIZE_MBYTES=64`, no explicit DTS placement (kernel picks
  location, plausibly near top of RAM).
- Boot chain: FSBL → U-Boot → Linux, `root=/dev/ram0`, rootfs unpacked as
  **initramfs** (important: this means `init=` cmdline arg is *not* honored —
  see below).

## Ruled out (tested and refuted, with evidence)

| Hypothesis | How tested | Result |
|---|---|---|
| DDR chip/config wrong | Compared FSBL DDR init table byte-for-byte vs vendor firmware; board photo confirmed MT41K256M16; `mtest` clean over full range | Identical / clean — not the cause |
| Z7010 only has 256 MB (external advisor doc claim) | Board photo + `bdinfo`/`mtest` | False, 512 MB confirmed present and healthy |
| `CONFIG_DMA_NONCOHERENT_MMAP` kernel-version regression (6.1 vendor vs 6.12 tezuka) | Checked actual `.config` — already force-selected via `select DMA_NONCOHERENT_MMAP if MMU` in arch/arm/Kconfig regardless of version | Not applicable, config already correct |
| IIO buffer mmap behavior changed 6.1→6.12 | Diffed `industrialio-buffer-dma(engine).c` between ADI's `adi-6.1.0` and our pinned 6.12.77 commit | Core `iio_dma_buffer_mmap()` logic unchanged (only `vm_flags_set()` API modernization) |
| AXI DMA bus width (64-bit vs 32-bit) mismatch vs vendor HDL | Changed `axi_ad9361_adc_dma`/`dac_dma` `DMA_DATA_WIDTH_SRC/DEST` 64→32 in `xilinx_ad9361.tcl`, rebuilt bitstream, matched vendor's asymmetric config, also matched in devicetree `adi,source/destination-bus-width` | Crash still occurred — width mismatch is real but not the (sole) cause |
| `maia-kmod` (`maia-sdr.c`) mmap/cacheinv logic writing outside its reserved regions | Full source review + diff against upstream v0.12.0 and against the file as directly edited by user (DMA_NON_COHERENT non-cached rewrite) | Both reserved regions confirmed `no-map`; userspace (`maia-httpd`) always mmaps at offset 0 (confirmed via `rxbuffer.rs`/`recording.rs` source) so the (real, now-fixed) missing offset-bounds-check was never actually reachable; driver's blast radius is confined to its own no-map regions, can't explain corruption of general RAM (e.g. PFN ~470 MB, far outside both regions) |
| ADI upstream PR #3282 "Fix iio dmaengine legacy fileio" (real list-corruption bug ADI fixed in `CONFIG_IIO_DMA_BUF_MMAP_LEGACY` fileio path) | Diffed PR contents directly against our pinned commit's `industrialio-buffer-dma.c` | Fix (the `if (queue->num_blocks)` guard, `fileio.active_block` NULL bracketing) **already present** in our tree — good, well-targeted lead that didn't pan out |
| `axi_dmac_terminate_all()` missing hardware STATUS-register poll before freeing descriptors (potential HW/SW race on DMA stop) | Read `dma-axi-dmac.c` `terminate_all`/`synchronize` — confirmed no readback of `AXI_DMAC_REG_STATUS` | Real code smell, but it's ~unmodified mainline code every AD9361/Pluto user runs — not confirmed as tezuka-specific; deprioritized, not patched |
| "maia-sdr FPGA write-DMA overruns its buffer into adjacent RAM" (would explain requiring maia-sdr to be active) | Killed by the `0x122` evidence (see below): real RF sample data is continuously varying, can't explain a **fixed** corruption value | Ruled out — do not need to go into `maia-hdl` Verilog |
| Split-test "which traffic type is required" (USB-gadget-only vs RF/IQ-only vs combined) | Ran isolated USB-only (clean), IQ-only (clean once, then crashed ~6 min later), combined (crashed ~4 min) | **Invalidated as a method** — corruption was later shown to occur at boot/login before any deliberate traffic is run at all, meaning the split tests were measuring timing noise on top of already-present corruption, not isolating a triggering subsystem |

## Fixes applied (real bugs, confirmed NOT the root cause on their own)

1. **`package/maia-kmod/0003-fix-rxbuffer-mmap-cacheinv-race-with-spinlock.patch`**
   — added `spinlock_t lock` to `maia_sdr_rxbuffer_drvdata`, made `mmap_done`
   check-and-set atomic across `mmap`/`vm_close`/`cacheinv`. Real TOCTOU race,
   extended survival time under test, did not eliminate the corruption.

2. **`maia-sdr.c` DMA_NON_COHERENT rewrite** (user's own change, later
   reviewed) — replaced `remap_pfn_range()` + manual ARMv7 cache invalidation
   (`v7_dma_inv_range`/`arm_cache_outer_inv_range`, kernel-version-fragile) with
   `vmf_insert_pfn()` fault-driven mapping + `pgprot_noncached()`. Architecturally
   sound simplification. Review found **two regressions** vs. the prior state,
   both fixed:
   - Dropped the `mmap_done` spinlock from fix #1 above — restored.
   - Dropped the `vma->vm_pgoff` offset+size bounds check before computing
     the PFN handed to `vmf_insert_pfn()` (which validates nothing) — restored
     in both `maia_sdr_recording_mmap()` and `maia_sdr_rxbuffer_mmap()`.
   Confirmed (via userspace source) offset is always 0 in practice, so this
   specific gap wasn't the active trigger, but is a legitimate latent
   out-of-bounds primitive worth having fixed regardless.

3. **DMA bus width fix** — `board/tezuka/nano/dts/nano.dtsi` `rx_dma`/`tx_dma`
   `adi,source/destination-bus-width` 64→32, plus matching HDL rebuild via
   `xilinx_ad9361.tcl`. Matches vendor firmware's asymmetric config. Real
   difference from vendor, did not eliminate the crash.

4. **`CONFIG_ENV_IS_NOWHERE=y`** (`uboot.config`) — unrelated to the crash;
   fixes U-Boot environment failure when booting under JTAG (Xilinx's
   `board.c` returns `ENVL_NOWHERE` for JTAG bootmode).

## Debug instrumentation added (kernel defconfig)

`board/tezuka/common/kernel/zynq_pluto_linux_defconfig`:
```
CONFIG_DEBUG_KERNEL=y
CONFIG_SLUB_DEBUG=y
CONFIG_DEBUG_LIST=y
CONFIG_DEBUG_VM=y
CONFIG_DEBUG_PAGEALLOC=y
# CONFIG_DEBUG_PAGEALLOC_ENABLE_DEFAULT is not set
CONFIG_PAGE_OWNER=y
```
All of these are boot-arg-gated (no cost unless enabled via cmdline), **except**
`CONFIG_PAGE_OWNER=y` which reserves per-page tracking metadata memory as soon
as it's compiled in when `page_owner=on` is passed — see OOM note below.

Do **not** set `CONFIG_SLUB_DEBUG_ON=y` (compile-default-on) — this was tried
first and OOM-panics during initramfs unpack on this memory-constrained board
(only ~215 MB in the `Normal` zone after the 256 MB+ reserved regions).
Boot-arg-gated `slub_debug=` scoped to specific caches is the safe alternative.

## Key evidence gathered via boot-arg debug instrumentation

Working boot-arg recipe (append to the normal `qspiboot` bootargs, no rebuild
needed for anything except `page_owner`/`kmalloc-256` scope changes):
```
slub_debug=FZPU,skbuff_head_cache,skbuff_fclone_cache,kmalloc-256 debug_pagealloc=on
```
(`slub_debug=FZPU` **unscoped** OOMs/hangs during initramfs unpack — always
scope it to specific cache names. `page_owner=on` also OOM-panics under the
current 256 MB reservation — needs the recording region shrunk first, see
Next steps.)

### 1. `debug_pagealloc` corruption catch (first hard evidence)
```
pagealloc: memory corruption
Call trace: do_wp_page from handle_mm_fault from do_page_fault from __dabt_usr
page: pfn:0x1d5de (~470 MB physical)
```
Caught via a genuine `debug_pagealloc` poison-mismatch: a page was freed
(poisoned), then handed back out, and part of its content had been silently
overwritten while it was supposedly free. Detected via an **ordinary userspace
COW page fault** — i.e. this has nothing to do with maia-sdr, USB, or
networking code paths; it's a page from the general pool that something wrote
into out-of-band. PFN ~470 MB is well outside both maia-sdr `no-map` regions
(which end at ~352 MB), which is why the maia-sdr-driver hypothesis doesn't fit
the physical address.

### 2. `free_swap_slot`/`check_new_page` NULL deref, both CPUs simultaneously
Hit on CPU0 and CPU1 at the same time, in `mosquitto_pub` and `api_controller.`
processes, during a "maiasdr + IQ combined" stress run. `check_new_page()` (a
page-allocator sanity check run when handing out a fresh page) read what looked
like garbage swap-cache metadata. Confirms same corruption class via yet
another unrelated path. (Note: the `free_swap_slot from 0x881` call-trace line
is a broken/garbage-return-address unwind — don't over-interpret that specific
frame, only the "NULL-ish deref during page-allocator sanity check" part is
solid.)

### 3. `handle_irq_desc` use-after-free (most concrete single finding)
```
Unable to handle kernel paging request at virtual address 4f90505c when read
PC is at handle_irq_desc+0x10/0x80
LR is at gic_handle_irq+0x70/0x84
Register r0 information: slab kmalloc-256 start c17ffc00 pointer offset 0 size 256
Process swapper/0 (pid: 0, stack limit = 0x0fc08bb0)
```
A real hardware interrupt fired (PID 0 / swapper, genuine IRQ context) and
crashed dereferencing its `irq_desc`. r0 (the `irq_desc` pointer) is confirmed
to point at the **start of a live `kmalloc-256` object** — not NULL, not wild —
but its content is no longer a valid `irq_desc`. This is a textbook interrupt
descriptor use-after-free: some driver freed/tore down an IRQ without properly
masking it at the hardware level first, and it fired again into memory that
had since been reallocated to something else. Occurred during IQ-alone traffic
(maia-sdr not active), pointing at the USB/RF DMA IRQ handling path rather than
maia-sdr.

### 4. Recurring fixed-value corruption fingerprint: `0x00000122`
```
list_add corruption. next->prev should be prev (c11c6340), but was 00000122.
```
(`lib/list_debug.c`, via `folio_batch_move_lru`/LRU list management, with
`CONFIG_DEBUG_LIST=y`.) **The exact same 4-byte value `0x00000122`** was seen
corrupting a `struct page.lru.prev` field here, and also showed up as the fault
address in the very first crash log captured at the start of this whole
investigation days earlier (`Unable to handle kernel NULL pointer dereference
at virtual address 00000122 when write`, via `free_pcppages_bulk`).

This is the most important clue: **a fixed, specific small-integer constant
repeating across independent runs is not consistent with a raw RF sample-data
DMA overrun** (real IQ sample data is continuously varying — it can't
deposit the same constant every time). It's much more consistent with a
**control value** (a buffer index, descriptor ID, or count) being written to
the wrong address — i.e. a wrong-pointer/off-by-something bug in some driver's
control/descriptor path, not a raw data-path overrun. This directly ruled out
pursuing the maia-sdr FPGA HDL (`maia-hdl` Verilog) as a lead.

Also notable: this list-corruption flood was observed to start firing
**immediately at `Nano login:`**, before any deliberate USB/RF stress test was
started that boot cycle — i.e. ordinary automatic boot-time AD9361 RX/TX
activation (`Switching to rfinput rx1` / `rfoutput tx1`, which happens on every
boot unconditionally) is apparently sufficient on its own, no manual stress
test required. This is what invalidated the earlier split-test results (see
"Ruled out" table) — Pass 1 (IQ-alone) being briefly "clean" was luck, not
absence of the bug.

## Current hypothesis

Not maia-kmod (`maia-sdr.c`), not the FPGA sample-data DMA path. Most likely a
**control-path bug in the base AD9361/axi_dmac/USB gadget driver stack** —
something writing a small control value (descriptor field, index, count) to a
stale/wrong address, most plausibly related to IRQ or DMA descriptor
lifecycle management, active from boot regardless of user-driven traffic.

## Bisect test results (2026-07-09, `rdinit=/bin/sh`)

Successfully executed the `rdinit=/bin/sh` bisect (see "Next steps" section
below for why `rdinit=` and not `init=` is required). Booted to a bare shell
with **zero** init scripts run, then manually brought components up one at a
time, checking `dmesg` for the `0x122`/list-corruption/any oops signature
after each addition. `/proc`, `/sys`, `/dev` (devtmpfs) had to be mounted by
hand first (none of that happens without init scripts either).

All of the following ran **cleanly, no corruption**, over a combined ~25+
minutes:

1. Kernel + platform driver probe alone (`ad9361_probe`, `cf_axi_adc`,
   `cf_axi_dds` all probed successfully from devicetree) — clean.
2. `insmod /lib/modules/6.12.77/updates/maia-sdr.ko` — clean.
3. Raw RX DMA streaming for 5+ minutes via the base IIO fileio path, manually
   driven through sysfs (`echo 1 > .../scan_elements/in_voltage{0,1}_en`,
   `buffer/length`, `buffer/enable`, then `cat /dev/iio:device3 > /dev/null`)
   — no maia-sdr, no USB, no maia-httpd involved at all — clean.
4. USB gadget brought up manually (`/etc/init.d/S23udc start` — mass storage +
   network composite gadget), including a full stop/reload cycle
   (`S23udc restart`) while RX DMA was still streaming — clean.
5. `udhcpd` started (`/etc/init.d/S40network` + `S41network`), `usb0`
   configured with a real IP, host-side DHCP/ARP traffic observed — clean.
6. **Genuine end-to-end USB IQ streaming** via the real `iiod`/libiio USB
   path (`iiod` was already running, launched by `S23udc` itself, bound to
   `/dev/iio_ffs` FunctionFS — this is the actual native PlutoSDR USB access
   method, not network-over-USB-gadget). Two `iio_readdev -u usb: ...`
   captures totaling 88 MB of real IQ data transferred over USB — clean.

**Conclusion so far: none of the individual pieces reproduce it in
isolation**, even combined and run well past the 4-6 minute window where the
full-stack tests reliably crashed.

**Follow-up: `maia-httpd` itself was then also brought up manually** (after
first mounting `/mnt/jffs2` and generating certs — see "Side effect" note
below) via its own init script, from this same bare-shell bisect environment
sitting on top of the already-clean RX DMA/USB-IQ/gadget stack:
- `maia-httpd` started and immediately began actively running the
  spectrometer engine in the background (confirmed via its own web API,
  `GET /api/spectrometer` returning live parameters) — this is maia-sdr's own
  FPGA write-DMA into the `maia_sdr_spectrometer` no-map region, running
  continuously by default the moment `maia-httpd` starts, with no client
  needed.
- The web UI (`https://192.168.2.1/`) was reachable and functional from the
  host machine over the USB network gadget interface.
- A recording was started via the web UI (exercises the *other* no-map
  region, `maia_sdr_recording`).
- **~10 minutes clean** with all of the above simultaneously active — no
  corruption.

**Combined cumulative result: over 35 minutes clean**, covering essentially
the entire RF/DMA/USB/maia-sdr/maia-httpd stack (platform probe, kernel
module, raw RX DMA, USB gadget + real USB IQ transfers via `iiod`, and now
`maia-httpd` itself with both the spectrometer and recording paths actively
running), assembled piece by piece from a bare shell. This is well past every
timeframe (seconds to ~6 minutes) in which the full standard-boot tests
reliably crashed this session.

**What's different between this bisect and a normal boot that still hasn't
been exercised:** the non-RF/non-USB daemons that a normal boot also starts
concurrently — `mosquitto`, `chronyd`, `crond`, `dropbear sshd`, `watchdog`,
`syslogd`/`klogd`, `MSD daemon` — none of which touch the AD9361/axi_dmac/
maia-sdr code paths directly, but which run on the *same* two CPUs
concurrently with everything above in a normal boot, and which this manual
bisect has not yet added. Also not yet ruled out: sheer elapsed time/
probability, since crash timing has been highly variable all session (some
crashes were near-instant, some took 10+ minutes) — 35 minutes clean is
strong but not certain proof of absence if the trigger is a rare race that
just hasn't fired yet.

**Side effect from this session worth noting:** manually invoking
`/etc/init.d/S50maia-sdr-certificates start` in the bare-shell bisect
environment triggered a `flash_erase` + fresh certificate regeneration on the
`/dev/mtd2` JFFS2 partition, because `/mnt/jffs2` wasn't mounted yet (that
script's fallback path is "erase and regenerate if not already mounted").
This wiped whatever was previously stored there (old certs, possibly cached
serial number) and replaced it with freshly generated self-signed certs. Not
believed to be relevant to the corruption bug, but is a real, unintended
change to persistent flash state from this debugging session.

**Further stress applied on top of the above (still clean):** with `maia-httpd`
+ spectrometer + recording all still active, ran **concurrent** USB IQ
streaming and USB network traffic simultaneously from the host: a 400 MB
`iio_readdev` capture (100M samples) running at the same time as a 1000-packet
ping flood (0% loss) and a loop of repeated HTTP downloads over the same USB
link (`curl` against `maia-httpd`'s static file server). All completed
successfully; `dmesg | grep -aiE "corruption|WARNING:|BUG:|Oops|panic|Unable
to handle"` returned **zero matches** afterward. This is the most aggressive
combined USB+RF load applied in this bisect session and is still fully clean
— cumulative clean time now comfortably over 40 minutes.

**Suggested next steps (not yet done):**
1. From the current bare-shell + full-RF-stack state (already running clean
   for 35+ min), incrementally add the remaining daemons one at a time
   (`mosquitto`, `dropbear sshd`, `watchdog`, `crond`, `chronyd`) via their
   own `/etc/init.d/S*` scripts, checking `dmesg` after each, to see if one of
   them — despite not touching AD9361/DMA code directly — is the actual
   missing ingredient (e.g. via memory pressure, scheduling interaction, or a
   genuine bug of its own independent of RF/USB).
2. In parallel/instead: just let the current full-RF-stack bisect state
   (maia-httpd + spectrometer + recording + USB IQ streaming) keep running
   much longer (tens of minutes to hours) to see if it eventually crashes on
   its own — this would show the RF/USB/maia-httpd stack alone *is*
   sufficient given enough time/probability, reframing the missing variable
   as "time" rather than "a specific untested daemon."

### UPDATE: full-stack reproduction achieved, decisive new evidence

All remaining daemons from a normal boot (`mosquitto`, `dropbear sshd`,
`watchdog`, `crond`, `chronyd`, `mdev`, `avahi-daemon`, `dbus-daemon`, `gpsd`,
`input-event-daemon`, and all the small `S0x`/`S2x` setup scripts) were then
started one at a time from the same bare-shell bisect session, on top of the
already-running RX DMA / USB gadget / maia-httpd / spectrometer / recording
stack — i.e. the environment was built up, piece by piece, into something
functionally equivalent to a full normal boot. All started cleanly
individually; `dmesg` stayed at zero corruption matches through this whole
process (~40+ min cumulative clean at this point).

With everything running, sustained **concurrent** load was then applied:
- `iio_readdev -u usb:` (native USB FunctionFS IIO path) — multiple runs,
  300M samples (1.2 GB) each, back to back.
- `iio_readdev -u ip:192.168.2.1` (the **RNDIS/network gadget** IIO path —
  confirmed to also work; `iiod` listens on both the FunctionFS and network
  interfaces simultaneously) — 400 MB transferred cleanly on its own first.
- Concurrent ping flood and repeated HTTP downloads from `maia-httpd`'s web
  server, all over the same USB link, running at the same time as the IQ
  streaming.

**After roughly 50+ minutes of cumulative bisect testing, this combined load
finally reproduced the corruption**, with a `debug_pagealloc` catch of unusual
clarity:
```
pagealloc: memory corruption
Call trace: do_wp_page from handle_mm_fault from do_page_fault from __dabt_usr
page: pfn:0xc3 (physical address ~0xC3000, ~780 KB — near the very BOTTOM of RAM)
raw: 00000000 00000100 00000122 00000000 000003ec 00000000 ffffffff 00000001
```

This is the most informative single catch of the whole investigation, for two
reasons:
1. **It confirms the missing ingredient really was "everything running
   concurrently"**, not any single component tested in isolation — none of
   platform-probe-alone, maia-sdr.ko-alone, raw RX-DMA-alone, USB-gadget-alone,
   or even maia-httpd-with-spectrometer-and-recording-alone reproduced it over
   many cumulative minutes; only the fully-assembled system under real
   concurrent multi-path load did.
2. **The PFN (`0xc3`, ~780 KB) is wildly different from the very first
   `debug_pagealloc` catch earlier this session (`0x1d5de`, ~470 MB)** — i.e.
   near the very bottom of physical RAM this time, versus near the top
   before. Combined with **`0x00000122` recurring at the same relative offset
   within the `struct page` raw dump, now confirmed across at least three
   independent crashes** (this one, the `list_add corruption... was 00000122`
   catch, and the very first NULL-deref-at-virtual-address-`00000122` crash
   from the start of the investigation), in different processes, at
   drastically different physical addresses — this is strong evidence of a
   **wrong/stale/dangling base pointer bug**, not a data-path overrun: something
   computes a target address from a corrupted or uninitialized pointer (which
   varies run to run, hence the wildly different PFNs), but always writes the
   *same* constant field value to it (hence `0x122` recurring exactly). A raw
   RF sample-data overrun could never produce a repeating fixed value across
   independent runs; a "write a fixed control/descriptor field via a bad
   pointer" bug explains it perfectly.

**Where this leaves the investigation:** the corruption needs genuine
concurrent multi-subsystem load to manifest in reasonable time (RF DMA +
gadget/network + maia-httpd's own buffer handling all active together,
matching real-world usage) — no single subsystem reproduces it alone even
after many minutes. The `0x122`-at-varying-PFN signature is the strongest lead
for whoever continues this: look for a fixed-value field write through a
pointer that could plausibly be stale/uninitialized/racing under concurrent
access — most likely candidates given everything ruled out so far are (a) the
IIO buffer block/descriptor bookkeeping in `industrialio-buffer-dma(engine).c`
under concurrent access from multiple simultaneous IIO contexts (both `usb:`
and `ip:` backends were active at points in this session, meaning two `iiod`
client connections may have been contending for the same buffer/device
concurrently — worth checking whether `iiod`/the kernel IIO buffer layer
correctly serializes or rejects concurrent opens from multiple contexts), or
(b) something in the USB gadget composite driver's function switching/
descriptor handling under concurrent FunctionFS + RNDIS + mass-storage
traffic. Neither has been directly inspected yet with this specific
"concurrent multi-context access" angle in mind — everything reviewed earlier
in this document (`maia-sdr.c`, `dma-axi-dmac.c`, `industrialio-buffer-dma.c`
vs PR #3282) was reviewed for single-stream-usage bugs, not concurrent-access
races.

Practical notes from this bisect session:
- `init=/bin/sh` does **not** work on this rootfs — confirmed via `Kernel
  command line:` printk showing it present but the full init sequence running
  anyway. Root cause: this system boots via `root=/dev/ram0` with the rootfs
  **unpacked as initramfs, no `switch_root`/`pivot_root`** — for that boot
  mode the kernel only honors `rdinit=` (defaults to `/init`); `init=` is only
  consulted after a real root switch, which never happens here.
- Catching the ~10s U-Boot autoboot window live over serial was the dominant
  time cost this session (many failed attempts). What actually worked
  reliably: start a background watcher polling the capture log for `"Hit any
  key"` *before* asking for the power cycle (removes human-reaction-time
  race), rather than trying to blindly time a keystroke burst after being told
  "power cycled" (reaction latency + the ~10s window is too tight, lost the
  race repeatedly).
- Confirmed (again) that stale/duplicate `cat /dev/ttyUSB0 > file` background
  processes from earlier attempts will keep running and silently steal bytes
  from new capture attempts if not explicitly killed — check `fuser
  /dev/ttyUSB0` and kill any leftover PIDs before starting a new capture.

## Next steps (in progress, not yet completed)

**Decisive test in progress: boot-time bisect via `rdinit=/bin/sh`.**
Because this rootfs is unpacked as an **initramfs with no `switch_root`**, the
correct kernel parameter to drop straight to a shell as PID 1 (skipping *all*
init scripts — no `maia_sdr.ko` insmod, no `maia-httpd`, no RF config) is
`rdinit=/bin/sh`, **not** `init=` (a first attempt using `init=/bin/sh` was
silently ignored by the kernel for this exact reason — confirmed via `Kernel
command line:` printk showing the parameter present but the full init sequence
running anyway).

Full bootarg for this test:
```
slub_debug=FZPU,skbuff_head_cache,skbuff_fclone_cache,kmalloc-256 debug_pagealloc=on rdinit=/bin/sh
```

Procedure once at the `rdinit=/bin/sh` prompt: check `dmesg` with **nothing
else done**.
- If the `0x122`/list-corruption flood is already present → base
  AD9361/axi_dmac platform path is guilty (probes from devicetree before init
  runs), maia-sdr is cleared entirely.
- If clean → manually `insmod maia_sdr.ko` and recheck, then bring up RF and
  recheck. Whichever step first produces it names the culprit.

This requires a **genuine power cycle** (not a watchdog/warm reset) immediately
before, so residual PL/DMA state from a prior crashed run doesn't contaminate
the result — the `no-map` reserved regions and PL bitstream state survive warm
resets.

Practical note: catching the ~10s U-Boot autoboot window live over serial has
been the main friction in this session (several attempts lost to stale/
duplicate `cat /dev/ttyUSB0` capture processes stealing bytes from each other,
and to `stty` baud reverting to 9600 after device re-enumeration — always
re-run `stty -F /dev/ttyUSB0 115200 raw -echo` immediately after the device
node reappears, before starting a new capture).

**If the bisect points at a software free-path** (rather than exonerating
everything down to a hardware-level issue): follow up with `page_owner=on`,
which requires first shrinking `maia_sdr_recording` in `nano.dtsi` from 256 MB
to ~32–64 MB (frees ~190-224 MB of headroom, avoids the OOM this option
currently causes) and rebuilding. `page_owner` prints the last legitimate
alloc/free call stack of the corrupted page directly, which would name the
guilty subsystem instead of requiring further inference.

## Session process notes / lessons

- **Always immediately resume JTAG-halted cores** after reading registers —
  leaving the target halted across multiple tool calls previously caused a
  false-alarm "crash" that was actually just the board being frozen by JTAG
  for several minutes (RCU stalls, USB gadget disconnect).
- **"Survived longer" is not evidence of a fix.** Crash timing has ranged from
  seconds to 10+ minutes across this whole investigation; every single-shot
  test that "ran longer" after a change (spinlock fix, DMA width match, the
  maia-sdr.c rewrite) was later shown to be timing noise, not a real
  improvement — corruption still occurred afterward.
- After any device power-cycle/re-enumeration, the serial port's baud rate
  reverts to a default (seen: 9600) — must explicitly reset to 115200 before
  each new capture, or output is unreadable garbage.
- Multiple background `cat /dev/ttyUSB0 > file` processes can end up alive
  simultaneously if a previous attempt's capture didn't cleanly exit; they
  silently split/steal bytes from each other, corrupting all captures
  involved. Check `fuser /dev/ttyUSB0` and kill stale PIDs by exact PID
  (never `pkill -f`, which has been observed to kill the entire background
  shell session) before starting a new one.
