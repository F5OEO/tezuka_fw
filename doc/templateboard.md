# Add board support: pciesdr7010

## Context

A new board, a PCIe-card-form-factor AD9361 SDR (schematic:
`doc/schematics/PCIE_7010_SDR-Schematic.pdf`), needs firmware support added to
tezuka_fw. The card carries its own Zynq SDR system (power from the PCIe
edge connector's +12V rail instead of USB) plus a separate, PCIe-attached
Realtek RTL8111F Gigabit Ethernet NIC wired straight to the PCIe edge
connector — that NIC is a peripheral for the **host** PC, not for the Zynq;
XC7Z010 has no GTP/GTX so the Zynq itself cannot be on the PCIe bus. It is
out of scope for this repo (no driver, no DTS node).

Schematic review + a diff against every existing board's Vivado config
(`ps7.tcl`/XDC in the maia-sdr checkout) settled the hardware questions:

- SoC symbols in the schematic are all labeled `XC7Z020CLG400`, but the user
  confirmed the physically populated part is a **7010** in the same
  pin-compatible `CLG400` package (a common cost-down since 7010/7015/7020
  share this package's pinout). Board key chosen: **`pciesdr7010`**.
- DDR (`MT41K256M16`, 32-bit bus), UART1 on MIO8/9, USB0 ULPI on
  MIO28-39/reset-on-MIO46, SD0 on MIO40-45, and the AD9361 LVDS digital
  interface (RX/TX data, frame, clocks all `_P`/`_N` pairs) all match
  `board/tezuka/fishball7010` and its FPGA counterpart
  `maia-hdl/projects/boards/fishball7010` exactly. fishball7010 uses a fixed
  clock scheme (no `vcxo_ctrl.tcl`), matching this board's fixed 40MHz/33.33MHz
  XOs. **fishball7010 is the correct clone template for everything except
  Ethernet and pin locations.**
- The RGMII PHY (RTL8211F) nets — `PHY_TXD0-3`, `PHY_RXD0-3`, `PHY_RXCLK`,
  `PHY_RXCTL`, `PHY_MDC`, `PHY_MDIO` — land on **PL** bank pins (banks 34/35/13
  in the schematic), not on PS `MIO 16..27` like fishball7010/7020. This
  needs GEM0 over **EMIO** + a `gmii_to_rgmii` PL converter — but
  `plutoskyr2` already does exactly this, with the same RTL8211F PHY. Its
  `maia-hdl/projects/boards/plutoskyr2/ports.tcl` (the `gmii_to_rgmii`
  instantiation + `ad_connect` wiring to `sys_ps7/GMII_ETHERNET_0` and
  `MDIO_ETHERNET_0`), `ps7.tcl` (`PCW_ENET0_ENET0_IO EMIO` /
  `PCW_ENET0_GRP_MDIO_IO EMIO`), and
  `board/tezuka/plutoskyr2/dts/fishball.dtsi`'s `gmii_to_rgmii_0`/`phy0` DT
  nodes are a directly reusable, proven pattern — this is now a copy, not new
  design work. Only the XDC pin locations (`system_constr.xdc:114-136` in
  plutoskyr2, `RGMII_rd/td/rxc/txc/rx_ctl/tx_ctl`, `MDIO_PHY_mdc/mdio_io`)
  need to move to this board's actual PL pin assignments.
- No OLED, no GPS on this board — drop fishball7020's OLED-specific DTS/kernel
  additions; fishball7010 already lacks them, so nothing to remove, just
  don't add them.
- Boot mode: 2-position DIP switch (JTAG/NAND/QSPI/SD per silkscreen), W25Q128
  (16MB) QSPI flash, SD card slot present — same u-boot-env QSPI offset
  scheme (`0xE00000`) as existing boards fits comfortably.

This plan covers two repos: `maia-sdr` (F5OEO fork, `/home/hp-z2-dev/prog/maia-sdr`)
for the FPGA bitstream, and this repo for Buildroot/kernel/U-Boot integration.

## 1. FPGA project (maia-sdr repo)

Directory: `maia-hdl/projects/boards/pciesdr7010/` — clone from
`maia-hdl/projects/boards/fishball7010/` (`ports.tcl`, `ps7.tcl`,
`system_constr.xdc`, `system_top.v`):

- `ps7.tcl`: keep DDR/UART1/USB0/SD0 params as-is (verified identical above).
  Change `PCW_ENET0_ENET0_IO` from `"MIO 16 .. 27"` to `EMIO`, keep
  `PCW_ENET0_GRP_MDIO_ENABLE`/`_IO` (MDIO can stay on MIO or also move to
  EMIO/PL — MDC/MDIO are on PL pins per schematic, so move MDIO to EMIO too).
- `ports.tcl`: base on fishball7010's (AD9361/board-specific BD wiring), and
  append plutoskyr2's RGMII block verbatim (`create_bd_intf_port` for
  `MDIO_PHY`/`RGMII`, `ad_ip_instance gmii_to_rgmii sys_rgmii` +
  `SupportLevel Include_Shared_Logic_in_Core`, and the `ad_connect` lines
  wiring `sys_rgmii` to `sys_ps7/GMII_ETHERNET_0`/`MDIO_ETHERNET_0`,
  `sys_ps7/FCLK_CLK1`, `sys_rstgen/peripheral_reset`, and the external
  `MDIO_PHY`/`RGMII` BD ports) — see
  `maia-hdl/projects/boards/plutoskyr2/ports.tcl:4-17`.
- `system_top.v`: base on fishball7010's, and add the same top-level
  `RGMII_rd/td/rxc/txc/rx_ctl/tx_ctl` + `MDIO_PHY_mdc/mdio_io` port
  declarations and BD-instance connections plutoskyr2's `system_top.v` has
  (`:39-46`, `:141-148`) — mechanical copy, no new Verilog logic.
- `system_constr.xdc`: re-pin every LOC to this board's physical assignments
  (AD9361 LVDS bus in banks 34/35/13, RGMII PHY pins, boot-mode DIP, LEDs,
  QSPI, SD, USB OTG, JTAG-via-FT2232 bank0). LVDS `IOSTANDARD`/`DIFF_TERM`
  pattern comes from fishball7010; the RGMII `IOSTANDARD LVCMOS25` pins +
  `create_clock -period 8.000 [get_ports RGMII_rxc]` (125MHz recovered clock)
  come from plutoskyr2's `system_constr.xdc:114-136,189-191` — only the pin
  letters change on both blocks, no new constraint types to derive.
- No `system_bd.tcl` change needed — the RGMII block lives entirely in the
  per-board `ports.tcl`, which is already selected via `PROJECT_NAME`; other
  boards' block designs are untouched by construction.
- `maia-hdl/projects/tezuka/system_project.tcl`: add a switch case
  `"pciesdr7010" { set p_device "xc7z010clg400-1" }`.
- Build: `cd maia-hdl/projects/tezuka && PROJECT_NAME=pciesdr7010 make`
  (per `CLAUDE.md`'s documented flow) — produces
  `pciesdr7010.sdk/system_top.xsa`, copied to
  `board/tezuka/pciesdr7010/bitstream/maia-iio/system_top.xsa` in this repo.
- FSBL: build separately via `xsct create_fsbl_project.tcl <board>` (see
  `todo/fsbl.txt` — the plain-unzip manual recipe there is broken, missing
  `xparameters_ps.h`; use the xsct method, and `rm -rf build/sdk` first since
  the script reuses a fixed, non-board-namespaced workspace/app name and
  silently leaves a stale, wrong-board `fsbl.elf` in place if it collides)
  — cannot reuse fishball7010's FSBL since DDR calibration/MIO strapping
  differs by board even if the PS7 IP config looks identical (board delay
  values are trace-length-calibrated; start from fishball7010's
  `PCW_UIPARAM_DDR_BOARD_DELAY0/1` and `DQS_TO_CLK_DELAY` as a first guess,
  verify/re-tune with a DDR memory test if boot is unstable).

## 2. tezuka_fw: board directory and defconfig

`board/tezuka/pciesdr7010/`, cloned from `board/tezuka/fishball7010/`:

- `dts/fishball.dts` + `.dtsi` → base on fishball7010's, update `model`
  string to `"PCIeSDR (Z7010/AD9361)"`; no OLED node needed. For Ethernet,
  drop fishball7010's MIO-based `&gem0` setup and copy plutoskyr2's EMIO/PL
  pattern verbatim from `board/tezuka/plutoskyr2/dts/fishball.dtsi:100-121`:
  `phy-mode = "rgmii-id"` + `gmii2rgmii-phy-handle`, and the `mdio { phy0
  (RTL8211F, id 0x001c.c916) ; gmii_to_rgmii_0 }` nodes. This is the same
  RTL8211F part the schematic calls out, so the PHY ID match is exact, not
  just structurally similar.
- `dts/zynq-7000.dtsi`: copy as-is from fishball7010 (base Zynq SoC DTSI is
  identical for any 7010; the `gem0`/`gem1` nodes plutoskyr2 uses are the
  same ones already in this file, just referenced differently from the
  board-level dtsi).
- `kernel/fragment/frag1.config`: start from fishball7010's
  (`CONFIG_DEBUG_ZYNQ_UART1=y`, `CONFIG_MACB=y`) — plutoskyr2's fragment
  needs nothing beyond `CONFIG_MACB=y` for its EMIO+RGMII setup either, so no
  extra Realtek/PHY config is needed beyond what's already in
  `board/tezuka/common/kernel/zynq_pluto_linux_defconfig`.
- `u-boot-dts/zynq-pluto-sdr.dts`: clone from fishball7010, adjust only if
  U-Boot needs to know about the DDR/MIO differences (mostly the same DTS
  reused for FSBL-DDR-init reference).
- `u-boot-config/board_frag.config`: identical, no diff seen between
  fishball7010/7020's — copy as-is.
- `uboot-env.txt`, `bitstream/fsbl.elf`, `bitstream/maia-iio/system_top.xsa`:
  populated from the FPGA build (§1).
- No `genimage-boot.cfg` needed unless fishball7010's is board-specific
  (check content — if generic, common/ handles it already).

`configs/pciesdr7010_defconfig`, cloned from `configs/fishball_maiasdr_defconfig`
(the fishball7010-backing defconfig):

- `BR2_TARGET_GENERIC_HOSTNAME="pciesdr7010"`
- Update every `board/tezuka/fishball7010/...` path to
  `board/tezuka/pciesdr7010/...` (kernel fragment, U-Boot DTS/config, DTS,
  `BR2_ROOTFS_POST_IMAGE_SCRIPT_ARGS`)
- `BR2_PACKAGE_BOARD_FPGA_BOARD="pciesdr7010"`
- Drop `BR2_PACKAGE_DATV`/OLED-only packages only if fishball7010's
  defconfig doesn't already have them (it doesn't, per the diff done above)
  — i.e. this defconfig needs no OLED-specific pruning, just a straight
  path-name clone.
- Keep `BR2_ROOTFS_POST_IMAGE_SCRIPT_ARGS` QSPI offset `0xE00000` (fits
  comfortably under the 16MB W25Q128).

## 3. boards.json / CI

Add `{"board": "pciesdr7010", "defconfig": "pciesdr7010_defconfig"}` to
`boards.json` so the CI matrix (`.github/workflows/main.yml`) and
`build.sh` both pick it up automatically.

## 4. Sequencing (de-risk FPGA vs. Buildroot plumbing independently)

1. **Buildroot plumbing first, against a placeholder bitstream**: copy
   fishball7010's `.xsa`/`fsbl.elf` into `board/tezuka/pciesdr7010/bitstream/`
   temporarily and run `./build.sh pciesdr7010` end-to-end. This proves out
   defconfig paths, DTS compile, kernel fragment, post-image script args,
   and zip output without depending on a correct new XDC/bitstream at all.
2. **FPGA bring-up in maia-sdr**: build the `pciesdr7010` Vivado project,
   fix constraint/timing errors, get a clean `system_top.xsa`.
3. **FSBL**: build against the new `.xsa`, verify DDR init (a flaky/no-boot
   DDR is the most likely first failure mode given re-guessed board-delay
   values — cross-check against Xilinx's DDR memory test / UART boot log).
4. **Swap in the real bitstream + FSBL**, rebuild
   (`board-fpga-reconfigure` then full rebuild per `CLAUDE.md`), verify boot
   over the JTAG-UART (FT2232, MIO8/9) to a login prompt.
5. **Ethernet bring-up**: low risk since it's a straight copy of plutoskyr2's
   proven EMIO+PL-RGMII pattern (same RTL8211F part) — verify link, `ethtool`, and
   throughput separately from RF bring-up.
6. **AD9361/RF bring-up**: `iio_info`, basic RX/TX loopback — should track
   fishball7010 closely given identical LVDS interface/DDR/PS config.

## Verification

- `make -C buildroot O=output/pciesdr7010 pciesdr7010_defconfig && make -C buildroot O=output/pciesdr7010` completes without error and produces
  correctly-sized images in `output/pciesdr7010/images/` (per `CLAUDE.md`'s
  "Verification" section — Buildroot post-image failures are often silent).
- `dtc -I dtb -O dts` on the built `.dtb` to sanity-check the Ethernet EMIO
  node and absence of stray OLED/GPS nodes.
- Physical bring-up on the board: serial console on UART1 (MIO8/9) via the
  onboard FT2232 JTAG/UART Type-C, confirm boot to login; `ethtool`/`ping`
  over the RGMII link; `iio_info` for the AD9361.
