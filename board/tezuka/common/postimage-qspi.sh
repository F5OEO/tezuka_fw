#!/bin/sh
set -e

COMMON_DIR="$(dirname "$0")"
BIN_DIR="$1"
# Args from BR2_ROOTFS_POST_IMAGE_SCRIPT_ARG in board config file
BOARD_DIR="$2"
dfu_suffix="$HOST_DIR/bin/dfu-suffix"

DEVICE_VID=0x0456
DEVICE_PID=0xb673

# Buildroot's host-bootgen (xilinx_v2025.2) may be broken.
# Test it, fall back to system bootgen if needed.
BOOTGEN="$HOST_DIR/bin/bootgen"
if ! "$BOOTGEN" -help >/dev/null 2>&1; then
    if [ -x /usr/bin/bootgen ]; then
        BOOTGEN=/usr/bin/bootgen
        echo "WARNING: host-bootgen is broken, using /usr/bin/bootgen"
    else
        echo "ERROR: host-bootgen is broken and no system bootgen found."
        echo "Install bootgen-xlnx: sudo apt-get install bootgen-xlnx"
        exit 1
    fi
fi

# ── Flash update (.frm / .dfu) ────────────────────────────────────────────────
# boot.img: FSBL + U-Boot only (no bitstream — FPGA loaded via pluto.itb FIT image)
# pluto.itb: FIT image bundling kernel + rootfs + bitstream + DTB
QSPIDIR="$BIN_DIR/flash"
SDIMGDIR="$BIN_DIR/sdimg"
JTAGDIR="$QSPIDIR/jtag"

echo "generating FIT image (pluto.itb)"
cp "$BOARD_DIR/plutomaia.its" "$BIN_DIR/plutomaia.its"
(cd "$BIN_DIR" && mkimage -f plutomaia.its pluto.itb)

echo "generating pluto.frm"
md5sum "$BIN_DIR/pluto.itb" | cut -d ' ' -f 1 > "$BIN_DIR/pluto.md5"
cat "$BIN_DIR/pluto.itb" "$BIN_DIR/pluto.md5" > "$BIN_DIR/pluto.frm"

# Flash-budget guard: $4 is the same QSPI-partition-size argument
# prepost-image.sh reads into FIT_SIZE (BR2_ROOTFS_POST_IMAGE_SCRIPT_ARGS'
# third value, e.g. 0x1E00000). Nothing previously checked pluto.frm against
# it -- an oversized image built and "succeeded" silently, only failing when
# a user tried to flash it.
FIT_MAX="${4:-$(grep "^fit_size=" "$BOARD_DIR/uboot-env.txt" 2>/dev/null | cut -d= -f2)}"
: "${FIT_MAX:=0x1E00000}"
FIT_MAX=$(printf '%d' "$FIT_MAX")
FRM_SIZE=$(stat -c%s "$BIN_DIR/pluto.frm")
echo "pluto.frm: ${FRM_SIZE} / ${FIT_MAX} bytes ($((FRM_SIZE * 100 / FIT_MAX))% of QSPI budget)"
if [ "$FRM_SIZE" -gt "$FIT_MAX" ]; then
	echo "ERROR: pluto.frm exceeds the QSPI budget by $((FRM_SIZE - FIT_MAX)) bytes" >&2
	exit 1
fi

echo "generating pluto.dfu"
"$dfu_suffix" -a "$BIN_DIR/pluto.itb" -v "$DEVICE_VID" -p "$DEVICE_PID"
mv "$BIN_DIR/pluto.itb" "$BIN_DIR/pluto.dfu"

echo "generating boot.img"
echo "img : {[bootloader] $BIN_DIR/fsbl.elf $BIN_DIR/u-boot.elf}" > "$BIN_DIR/boot.bif"
"$BOOTGEN" -image "$BIN_DIR/boot.bif" -w -o i "$BIN_DIR/boot.img"

echo "generating boot.frm"
cat "$BIN_DIR/boot.img" "$BIN_DIR/uboot-env.bin" "$COMMON_DIR/target_mtd_info.key" | \
	tee "$BIN_DIR/boot.frm" | md5sum | cut -d ' ' -f1 | tee -a "$BIN_DIR/boot.frm"

echo "generating boot.dfu"
cp "$BIN_DIR/boot.img" "$BIN_DIR/boot.bin.tmp"
"$dfu_suffix" -a "$BIN_DIR/boot.bin.tmp" -v "$DEVICE_VID" -p "$DEVICE_PID"
mv "$BIN_DIR/boot.bin.tmp" "$BIN_DIR/boot.dfu"

echo "generating uboot-env.dfu"
cp "$BIN_DIR/uboot-env.bin" "$BIN_DIR/uboot-env.bin.tmp"
"$dfu_suffix" -a "$BIN_DIR/uboot-env.bin.tmp" -v "$DEVICE_VID" -p "$DEVICE_PID"
mv "$BIN_DIR/uboot-env.bin.tmp" "$BIN_DIR/uboot-env.dfu"

cp "$BIN_DIR/boot.dfu" "$BIN_DIR/boot.frm" "$BIN_DIR/pluto.dfu" "$BIN_DIR/pluto.frm" $QSPIDIR

# JTAG: strip u-boot.elf for the recovery bundle. Locate the cross strip by
# glob rather than hardcoding a toolchain prefix -- $HOST_DIR/bin/*-strip is
# arm-linux-strip for Bootlin, arm-buildroot-linux-gnueabihf-strip for the
# internal toolchain, arm-none-linux-gnueabihf-strip for Arm GNU. This file
# never reaches pluto.frm (JTAG-only), so a missing strip degrades to an
# unstripped elf rather than aborting the build.
CROSS_STRIP=""
for _s in "$HOST_DIR"/bin/*-strip; do
	[ -x "$_s" ] || continue
	CROSS_STRIP="$_s"
	break
done
if [ -n "$CROSS_STRIP" ]; then
	"$CROSS_STRIP" "$BIN_DIR/u-boot.elf"
else
	echo "WARNING: no cross strip found in $HOST_DIR/bin, shipping unstripped u-boot.elf"
fi
cp "$BIN_DIR/u-boot.elf" "$JTAGDIR"
cp "$BOARD_DIR/bitstream/fsbl.elf" "$JTAGDIR"
cp "$BR2_EXTERNAL/tools/jtag-recovery/xilinx-tcl.cfg" "$JTAGDIR"
cp "$BR2_EXTERNAL/tools/jtag-recovery/boot_fsbl_uboot.sh" "$JTAGDIR"
cp "$BR2_EXTERNAL/tools/jtag-recovery/boot_fsbl_uboot.bat" "$JTAGDIR"
cp "$BR2_EXTERNAL/tools/jtag-recovery/tezuka.cfg" "$JTAGDIR"