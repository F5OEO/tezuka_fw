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
mkdir -p "$QSPIDIR"

echo "generating FIT image (pluto.itb)"
cp "$BOARD_DIR/plutomaia.its" "$BIN_DIR/plutomaia.its"
(cd "$BIN_DIR" && mkimage -f plutomaia.its pluto.itb)

echo "generating pluto.frm"
md5sum "$BIN_DIR/pluto.itb" | cut -d ' ' -f 1 > "$BIN_DIR/pluto.md5"
cat "$BIN_DIR/pluto.itb" "$BIN_DIR/pluto.md5" > "$BIN_DIR/pluto.frm"

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