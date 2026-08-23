# DDR3 memory reliability issue — OpenSourceSDRLab PCIe SDR (AD9363, XC7Z010)

**Product:** OpenSourceSDRLab PCIe version PlutoSDR, AD9363, XC7Z010
(https://opensourcesdrlab.com/products/opensourcesdrlab-pcie-version-plutosdr-ad9363-xc7z010?VariantsId=10329)

## Summary

The board exhibits DDR3 memory corruption under normal operating load,
reproducible on **both** the manufacturer's own shipped firmware and an
independently-built third-party firmware. The corruption is confined to a
specific 16-bit half of the 32-bit data bus, occurs at varying memory
addresses, and is present regardless of DDR timing-parameter tuning across
the full range of values known to work on other Zynq-7010 boards. This
points to a hardware/PCB-level issue (signal integrity or a marginal
component) rather than a firmware or software defect.

## Symptom

Under normal use the board's Linux kernel intermittently crashes with a
wide variety of signatures — kernel panics, memory-management faults, and
occasional total hangs — always traceable back to corrupted data in RAM.
The specific crash location varies from boot to boot (page-fault handling,
process creation, IRQ handling, etc.), which is itself a signature of
random memory corruption rather than a software logic bug: a real bug in
one code path produces the same crash every time, while ours does not.

## Direct evidence: `memtester` fails identically on both firmwares

We ran the standard `memtester` memory-test utility on the board's DRAM,
first on our own third-party firmware build, and then — to rule out any
possibility that this was caused by our own firmware or configuration —
booted the **manufacturer's own, unmodified shipped SD card image** and
ran the same tool there (cross-compiled statically since the stock image
doesn't include it).

**Both firmwares fail within seconds, with the identical error signature:**
every corrupted 32-bit word has its correct high 16 bits, but wrong low 16
bits. Examples (identical pattern on both firmwares):

```
FAILURE: 0xffff0020 != 0xffffffdf at offset 0x012585fc
FAILURE: 0x13131212 != 0x13131313 at offset 0x0025429c
FAILURE: 0x00000000 != 0x0000ffff at offset 0x001d2844
```

The failing address is different every run (not a stuck/dead cell), but is
always confined to the same 16-bit half of the bus, pointing at a
write/read-capture timing or signal-integrity issue specific to one of the
two `MT41K256M16` (x16) DDR3 chips that make up the 32-bit bus.

On the manufacturer's firmware (older 5.15 kernel) the test ran to
completion without crashing the OS, logging dozens of these corrupted
words per pass. On our own firmware (newer 6.12 kernel) the same
corruption typically escalates into an OS crash within the same test
window — we believe this is just because the newer kernel is more exposed
to secondary corruption effects (e.g. corrupted page-table structures),
not evidence of a different underlying fault.

## Ruled out: DDR timing-parameter tuning

Before concluding this is a hardware issue, we tried retuning the two
Zynq-7 PS DRAM controller parameters that account for board-specific
trace-length variation (`DQS_TO_CLK_DELAY` and `BOARD_DELAY`), rebuilding
the FSBL/bitstream and re-testing for each value:

| Value tried | Source | Result |
|---|---|---|
| `DQS_TO_CLK_DELAY_0 = 0.025` | known-good value from a related board design | No change |
| `DQS_TO_CLK_DELAY_0 = 0.095` | known-good value from a different related board design | No change |
| `BOARD_DELAY0 = 0.202` | known-good value from a different related board design | No change |

All three values are ones that are known to work correctly on other,
electrically similar boards in the same product family — i.e. this covers
the practical working range, not arbitrary guesses. None of them changed
the failure signature. Since the Zynq DDR controller's automatic
write/read-eye training (`TRAIN_DATA_EYE`) searches for a stable point
around whatever seed value it's given, failing at every value that's ever
worked elsewhere is strong evidence this isn't a "wrong calibration value"
problem.

We also confirmed at the U-Boot level (before Linux, before any FPGA
fabric memory traffic starts) that simple read/write memory checks over
the same address ranges are completely stable with no errors — the fault
only appears once the system is under real memory-access load (Linux +
FPGA-side traffic), which is consistent with a signal-integrity/timing
margin issue rather than a dead memory cell.

## Consistent with known issues on this Zynq-7000 DDR3 hard IP

We found two threads on AMD's own Adaptive Support forum describing
functionally identical symptoms on other Zynq-7000/DDR3 board designs:

- https://adaptivesupport.amd.com/s/question/0D54U00005uAyXfSAK/zynq-7020-ddr3l-interface-not-reliable-at-full-speed-settings-to-tweak
  — `memtester` failures specific to certain bit patterns, not reproducible
  without real FPGA-side memory traffic (matching our own U-Boot-level
  finding), eventually traced to crosstalk between data and address lines
  visible on an oscilloscope.
- https://adaptivesupport.amd.com/s/question/0D54U00008PzEEQSA3/ddr3-inconsistent-word-errors-with-zynq7020
  — the reporter explicitly tried tuning `DQS_TO_CLK_DELAY` and
  `BOARD_DELAY` and found it made **no difference at all**, matching our
  own result exactly. What resolved their case was physically increasing
  the DDR address/clock line termination resistor value (from the
  datasheet-suggested ~40Ω to ~90Ω) — a board-level hardware change.

Both threads confirm that on this DRAM controller, these delay parameters
are a necessary baseline for the automatic training to function at all,
not a general-purpose fix for marginal signal integrity once that baseline
is met — which matches what we found on this board.

## Request

Given the evidence above rules out our own firmware/software as the
cause, we'd appreciate:

1. Confirmation of whether this is a known issue on this board revision,
   and whether a board-level fix (e.g. a termination or layout change,
   as in the second forum thread above) has been identified.
2. If this is a per-unit defect
3. Any guidance on DDR termination/layout for this specific design would
   also be useful to us independent of the warranty question, since we may
   end up needing to characterize other units of the same board.


