#!/bin/sh
set -e

COMMON_DIR="$(dirname "$0")"
BIN_DIR="$1"
# Args from BR2_ROOTFS_POST_IMAGE_SCRIPT_ARG in board config file
BOARD_DIR="$2"
DTB_NAME="$3"

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

# ── SD card ───────────────────────────────────────────────────────────────────
# BOOT.bin: FSBL + bitstream + U-Boot (bitstream embedded so FPGA is loaded at power-on)
# Kernel and rootfs are separate files loaded by U-Boot from the FAT partition.
# Requires: fsbl.elf, u-boot.elf, system_top.bit, system_top.bit.bin, Image.lzma
# (produced by postimage-qspi.sh when called via post-image.sh)

SDIMGDIR="$BIN_DIR/sdimg"


echo "generating BOOT.bin"
echo "img : {[bootloader] $BIN_DIR/fsbl.elf [load = 0x1000000] $BIN_DIR/system_top.bit $BIN_DIR/u-boot.elf}" > "$SDIMGDIR/boot.bif"
"$BOOTGEN" -image "$SDIMGDIR/boot.bif" -w -o i "$SDIMGDIR/BOOT.bin"

if [ -e "$BOARD_DIR/bitstream/overclock/" ]; then
    mkdir -p "$SDIMGDIR/overclock"
    for filename in "$BOARD_DIR/bitstream/overclock/"*.elf; do
        echo "img : {[bootloader] $filename [load = 0x1000000] $BIN_DIR/system_top.bit $BIN_DIR/u-boot.elf}" > "$SDIMGDIR/boot.bif"
        NAME=$(basename -- "$filename" .elf)
        "$BOOTGEN" -image "$SDIMGDIR/boot.bif" -w -o i "$SDIMGDIR/overclock/BOOT_${NAME}"
    done
fi

rm "$SDIMGDIR/boot.bif"

cp "$BIN_DIR/system_top.bit.bin" "$SDIMGDIR/system_top.bin"

# uboot does not decompress the ramdisk — kernel handles it
mkimage -A arm -T ramdisk -C none -d "$BIN_DIR/rootfs.cpio.xz" "$SDIMGDIR/uramdisk.image.xz"
mkimage -A arm -O linux -T kernel -C lzma -a 0x8000 -e 0x8000 \
	-n "Linux kernel" -d "$BIN_DIR/Image.lzma" "$SDIMGDIR/uImage"

cp "$BIN_DIR/$DTB_NAME" "$SDIMGDIR/devicetree.dtb"
cp "$COMMON_DIR/uboot-env.txt" "$SDIMGDIR/uEnv.txt"


