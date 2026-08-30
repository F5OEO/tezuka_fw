# FPGA resource usage (plutoskyr2)

Generated from a Vivado hierarchical utilization report (`report_utilization -hierarchical`)
run against the routed checkpoint of the `plutoskyr2` maia-hdl build
(`maia-hdl/projects/tezuka`, device `xc7z020clg484-2`).

Device totals: 53200 LUTs, 106400 FFs, 140 BRAM36-equivalent, 220 DSP slices.

## Top-level totals

| Resource | Used | Available | Util% |
|---|---|---|---|
| LUTs | 23005 | 53200 | 43% |
| FFs | 32111 | 106400 | 30% |
| BRAM36 / BRAM18 | 46 / 19 | 140 / 280 | - |
| DSP | 106 | 220 | 48% |

## Component breakdown (Total LUTs / FFs / BRAM36+18 / DSP), largest first

| Component | LUTs | FFs | BRAM36/18 | DSP | What it is |
|---|---|---|---|---|---|
| `axi_ad9361` | 7562 | 12263 | 0/0 | 28 | ADI AD9361 IIO interface core (biggest single block) |
| `dvbs2_encoder_wrapper_0` | 4888 | 4647 | 16/0 | 2 | DVB-S2 encoder |
| `maia_sdr` | 3414 | 3528 | 21/16 | 18 | Maia-SDR RX (spectrometer/waterfall/sweep) |
| `axi_hp2_interconnect` | 875 | 1457 | 3/2 | 0 | AXI HP2 interconnect (DMA path glue) |
| `rrc_2interpol` | 753 | 3124 | 0/0 | 38 | RRC pulse-shaping interpolation filter (TX) — DSP-heavy, LUT-light |
| `axi_gp0_interconnect` | 702 | 683 | 0/0 | 0 | AXI GP0 interconnect |
| `switchfir` | 668 | 639 | 0/0 | 0 | FIR select/switch mux |
| `tx_fir_interpolator` | 564 | 1297 | 0/0 | 20 | TX FIR interpolator |
| `cs12_8mux_chan0` / `cs12_8mux_chan1` | 540 / 538 | 461 / 461 | 0/0 | 0 | 12→8 constellation-symbol mux (2 channels, DVB-S2 TX path) |
| `switchsrc` | 508 | 466 | 0/0 | 0 | Source switch mux |
| `axi_ad9361_adc_dma` | 445 | 665 | 1/0 | 0 | ADC DMA |
| `axi_ad9361_dac_dma` | 395 | 486 | 1/0 | 0 | DAC DMA |
| `switchdest` | 277 | 219 | 0/0 | 0 | Destination switch mux |
| `sys_rgmii` | 135 | 235 | 0/0 | 0 | RGMII Ethernet PHY interface (plutoskyr2-specific, EMIO) |
| `util_ad9361_dac_upack` | 114 | 128 | 0/0 | 0 | DAC unpack |
| `util_ad9361_adc_pack` | 108 | 130 | 0/0 | 0 | ADC pack |
| `interclk_i` / `interclk_q` | 65 / 65 | 132 / 132 | 0/0 | 0 | I/Q inter-clock CDC |
| `cs12_sync_chan0` / `cs12_sync_chan1` | 54 / 55 | 51 / 51 | 0/0 | 0 | cs12 sync (2 channels) |
| `interclk` | 55 | 90 | 1/0 | 0 | inter-clock domain crossing |
| `util_ad9361_divclk_reset` | 17 | 34 | 0/0 | 0 | AD9361 divider clock reset |
| `dma_domain_fifo` | 26 | 33 | 1/1 | 0 | DMA clock-domain FIFO |
| `dw_rrc_in` | 23 | 102 | 0/0 | 0 | RRC filter input width converter |
| `dw_enc_out` | 12 | 105 | 0/0 | 0 | Encoder output width converter |
| `util_ad9361_adc_fifo` | 12 | 101 | 1/0 | 0 | ADC FIFO |
| `dw_rrc_out` | 10 | 102 | 0/0 | 0 | RRC filter output width converter |
| `sys_ps7` | 15 | 22 | 0/0 | 0 | Zynq PS7 (glue logic only; PS itself is hard IP) |
| everything else (muxes/slices/reset/clk glue) | <10 each | — | 0/0 | 0 | trivial |

Note: the sum of lower-level cells can exceed the parent's total due to cross-hierarchy LUT combining.

## Grouped view

- **RX chain** (`axi_ad9361` + `maia_sdr` + ADC/DAC DMA + pack/unpack): ~11.9k LUTs, ~17.1k FFs, ~9 DSP —
  dominated by the `axi_ad9361` ADI IP itself, not by Maia-SDR's own logic.
- **DVB-S2 TX chain** (`dvbs2_encoder_wrapper` + `rrc_2interpol` + `tx_fir_interpolator` + `cs12_*mux` +
  `switch*`): ~8.7k LUTs, ~11.6k FFs, 16 BRAM36, 60 DSP — DSP-dominated, driven mainly by
  `rrc_2interpol` and `tx_fir_interpolator`.
- **PS7 / interconnect / glue**: ~1.6k LUTs, ~2.1k FFs, mostly AXI interconnects (`axi_gp0_interconnect`,
  `axi_hp2_interconnect`).

## Regenerating this report

```bash
source /home/hp-z2-dev/prog/maia-sdr/sourceme.first
vivado -mode batch -nojournal -nolog -source <(cat <<'EOF'
open_checkpoint maia-hdl/projects/tezuka/plutoskyr2.runs/impl_1/system_top_routed.dcp
report_utilization -hierarchical -hierarchical_depth 3 -file hier_util.rpt
EOF
)
```

Run from inside the `maia-sdr` checkout (`/home/hp-z2-dev/prog/maia-sdr`), after a completed
`plutoskyr2` build (or swap in another board's `<board>.runs/impl_1/system_top_routed.dcp`).

## Fitting this design on a Zynq-7010

XC7Z010 budget: 17,600 LUTs / 35,200 FFs / 60 BRAM36-equivalent / 80 DSP.

Against the plutoskyr2 numbers above, this overflows LUTs by ~31% and DSP by ~32%; FFs and BRAM
have headroom. Reduction levers, in priority order:

1. **DSP (sharpest overflow, easiest lever)**: `rrc_2interpol` (38 DSP) + `tx_fir_interpolator`
   (20 DSP) alone account for 58 of the 106 DSPs used — more than the entire 7010 budget.
   Shortening these FIR filters (fewer taps / coarser rolloff, or a polyphase/time-multiplexed
   structure trading DSP for LUT+control logic) is the highest-leverage single change.
   Tradeoff: filter performance (rolloff steepness) vs. DSP count.

2. **LUTs (the harder problem)**: `axi_ad9361` (7562) + `dvbs2_encoder_wrapper` (4888) +
   `maia_sdr` (3414) alone sum to ~15.9k, leaving almost no room for the TX filter chain or
   interconnects. Check whether `axi_ad9361` is instantiated 2T2R in `system_bd.tcl` for
   plutoskyr2 — switching to 1T1R (SISO, like stock Pluto hardware) would meaningfully shrink
   that IP's LUT/FF/DSP footprint. Tradeoff: loses MIMO/dual-channel capability.

3. **Structural fallback**: if (1) and (2) aren't enough, split into two bitstream variants for
   the 7010 instead of one that does everything — an RX-only build (`maia_sdr` + `axi_ad9361`,
   dropping the whole DVB-S2 TX chain: encoder + RRC + FIR + cs12 muxes) or a TX-only build.
   Tradeoff: each variant keeps full quality of its mode, but the 7010 board can't do RX
   spectrum analysis and DVB-S2 TX simultaneously in one bitstream.

### Verified: axi_ad9361 is configured 2R2T, not 1T1R

`system_bd.tcl` sources `maia-hdl/projects/common/xilinx_ad9361.tcl`, which instantiates
`axi_ad9361` with `CONFIG.MODE_1R1T 0` unconditionally (both the LVDS and CMOS branches, lines
~114/118) — `0` on ADI's IP means **2R2T (dual-channel/MIMO)**, not single-channel. Consistently,
the surrounding FIFO/pack/unpack blocks (`util_ad9361_adc_fifo`, `util_ad9361_adc_pack`,
`util_ad9361_dac_upack`) are all set to `NUM_OF_CHANNELS 4` (2 channels x I/Q), matching 2R2T.

This confirms lever #2: setting `MODE_1R1T` to `1` and dropping the FIFO/pack channel counts from
4 to 2 would move to single-channel (SISO), like stock Pluto hardware, and should meaningfully cut
`axi_ad9361`'s LUT/FF/DSP footprint by removing the second channel's datapath. Not yet measured
how much this actually saves — would need a rebuild + re-run of the hierarchical utilization
report above to quantify.

Still not yet verified: RRC/FIR tap counts (lever #1) — check the RRC filter generator/params in
maia-hdl before committing to a specific tap-count cut.

## 7010 reduction log (measured)

Changes are landed and rebuilt one at a time (each is a real Vivado rebuild + re-run of the
hierarchical utilization report above) so savings can be attributed to a specific cause. All
edits are in the `maia-sdr` checkout, not this repo.

### 1. Disabled `dac_dds` for plutoskyr2 — DONE, measured

`system_bd.tcl`'s `plutoskyr2` case had `set dac_dds "dac_dds"`; commented out (matching how
`fishball7020` already does it). With `dac_dds` unset, `xilinx_ad9361.tcl`'s else-branch sets
`DAC_DDS_DISABLE 1` (previously `0`) — `TDD_DISABLE` was already `1` either way, so this is the
only change. RTL confirms `DAC_DDS_DISABLE` gates a real `generate` block per TX channel in
`axi_ad9361_tx_channel.v` (the per-channel DDS/NCO test-tone generator), not just a mux.

| Resource | Before | After | Delta |
|---|---|---|---|
| Top LUTs | 23005 | 18419 | **-4586** |
| Top FFs | 32111 | 26020 | -6091 |
| Top DSP | 106 | 98 | -8 |
| `axi_ad9361` LUTs | 7562 | 2968 | -4594 |
| `axi_ad9361` FFs | 12263 | 6173 | -6090 |
| `axi_ad9361` DSP | 28 | 20 | -8 |

Bigger than expected — the DDS test-tone generator inside `axi_ad9361` (2 channels' worth of
phase accumulators + sine/cosine tables) was a much larger chunk of that IP than assumed. This
single change already closes most of the LUT gap: 18,419 vs. the 17,600 7010 budget (only ~5%
over now) and 98 vs. 80 DSP (~23% over).

**Functional tradeoff**: `board/tezuka/common/overlay_tezuka/root/api_controller.sh`'s MQTT
`siggen/*` topics (test-tone frequency/scale/enable) read/write
`cf-ad9361-dds-core-lpc` IIO attributes. With the DDS core removed, `dds_folder` resolves empty
and those siggen commands silently no-op — the on-target test-tone signal generator feature is
lost on this board. Everything else (normal RX/TX, DVB-S2 TX path) is unaffected.

### 2. MODE_1R1T (2R2T → 1R1T), gated to plutoskyr2 only — DONE, measured

Added `set mode1r1t "mode1r1t"` to the `plutoskyr2` case only in `system_bd.tcl` (other 13
boards stay bit-identical — `xilinx_ad9361.tcl` is shared and now branches on
`if {[info exists mode1r1t]}`, defaulting to the old `MODE_1R1T 0` when unset). RTL confirms
`MODE_1R1T` gates a real `generate` block (`axi_ad9361_rx_channel` / `axi_ad9361_tx_channel`
disable) for the second RX/TX channel — not framing-only.

Software-side, this is safe by default: plutoskyr2's u-boot env (`adi_loadvals_pluto` in
`board/tezuka/plutoskyr2/uboot-env.txt`) already defaults `mode` to `1r1t` and removes
`adi,2rx-2tx-mode-enable` from the DTS at boot in that case — so the AD9361 chip itself is
already running SISO by default today, on a fabric built for MIMO. Hardcoding the fabric to 1R1T
matches today's default and only removes the (apparently unused for this board) ability to
`setenv mode 2r2t` at runtime.

| Resource | Before (post-DDS) | After | Delta |
|---|---|---|---|
| Top LUTs | 18419 | **17388** | -1031 |
| Top FFs | 26020 | 23466 | -2554 |
| Top DSP | 98 | 88 | -10 |
| `axi_ad9361` LUTs | 2968 | 1947 | -1021 |
| `axi_ad9361` FFs | 6173 | 3677 | -2496 |
| `axi_ad9361` DSP | 20 | 10 | -10 |

**17,388 LUTs now fits under the 7010's 17,600 budget** (~99% utilized — fits, but with very
little routing margin; not yet timing-closed on real 7010 fabric). DSP is down to 88, still 8
over the 80 budget — the FIFO removal below doesn't touch DSP, so DSP still needs lever #1
(RRC/FIR tap reduction) regardless.

### 3. Remove FMCOMMS2-style rate FIFOs — investigated, NOT VIABLE as scoped, do not attempt

Originally planned: delete `util_ad9361_adc_fifo` (`util_wfifo`) and `axi_ad9361_dac_fifo`
(`util_rfifo`) plus the `util_ad9361_divclk` / `_divclk_reset` / `_divclk_sel` chain, reclocking
`util_ad9361_adc_pack` / `util_ad9361_dac_upack` / the DMAs directly off `axi_ad9361/l_clk`. Two
findings kill this plan — **no code was changed for this step**:

- `maia.tcl:38-42` wires `util_ad9361_adc_fifo/dout_data_0` / `dout_data_1` straight into
  `maia_sdr/re_in` / `im_in`. This FIFO is not DMA-capture plumbing — it's Maia-SDR's real-time
  RX input path. Removing it kills RX entirely, not just IQ-recording-to-DMA.
- `util_ad9361_divclk/clk_out` is the design's shared sample-rate clock, consumed well beyond
  the FIFOs: `maia_sdr/sampling_clk` (maia.tcl:46), `rx_fir_decimator/aclk` (rxfir.tcl),
  `tx_fir_interpolator/aclk` + its `delay_tvalid` cells (txfir.tcl), `fft_overlap/clk` +
  `interclk_i`/`interclk_q` (fftoverlap.tcl), the DVB `interclk` (dvb.tcl), `myiqburst/iq_clk`
  (iqburst.tcl), and `vcxo_ctrl/reset` (plutoskyr2's vcxo_ctrl.tcl). Deleting the divider breaks
  essentially every processing block in the design, not just the AD9361 interface.

**This restructure also could not have closed the 7010 gap even if it worked**: both FIFOs are
0 DSP, and the whole divclk chain is ~50 LUT / 2 RAMB36. After changes 1+2, DSP (88 vs. 80
budget) is the only resource still over — LUT already fits (see below) — so this entire lever
was pointed at the wrong resource.

If this is revisited later, `maia-hdl/projects/common/xilinx_ad9361_no_pack.tcl` is an existing
alternate variant of this file (skips `cpack2`/`upack2` packing) worth reading first — someone
already built a related variant, so check what it actually does before assuming a rewrite from
scratch.

## Current state vs. 7010 budget (after changes 1+2)

| Resource | Used | 7010 budget | Margin |
|---|---|---|---|
| LUTs | 17388 | 17600 | fits, ~212 LUT / 1.2% — thin, unproven on real 7010 routing/timing |
| FFs | 23466 | 35200 | comfortable |
| BRAM36-eq | ~55.5 (46 RAMB36 + 19 RAMB18) | 60 | fits, ~92.5% utilized — second tight resource |
| DSP | 88 | 80 | **over by 8 — the only remaining hard blocker** |

DSP is now the sole gap. `rrc_2interpol` (38) + `tx_fir_interpolator` (20) = 58 of the 88 DSPs
used (`axi_ad9361` dropped to 10 DSP after 1R1T). With only ~1.2% LUT margin left, a
polyphase/time-multiplexed filter restructure (DSP-for-LUT trade) is the wrong direction — it
spends the resource already nearly exhausted. A straight tap-count reduction on `rrc_2interpol`
and/or `tx_fir_interpolator` (fewer taps, coarser rolloff) removes multipliers without adding
control-logic LUTs, and should be checked against the RRC/FIR generator parameters in maia-hdl
before committing to a specific cut — not yet done.

**Not yet done: functional verification.** Both landed changes (DDS disable, 1R1T) only have
resource-count evidence (`report_utilization` on the routed checkpoint) — neither has been
tested on real hardware. A physical plutoskyr2 board is available; before building further on
top of these two changes, flash the resulting bitstream and confirm RX/TX still work (see
"Testing a binary on a physical board" / boot-artifact update recipes in the top-level CLAUDE.md).
