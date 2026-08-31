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
# BOOT.bin: FSBL + U-Boot only (no bitstream — U-Boot programs the FPGA at
# runtime via `fpga load` from system_top.bin on the FAT partition, see
# fpgaload_mmc/sdboot_ram in uboot-env.txt).
# Kernel and rootfs are separate files loaded by U-Boot from the FAT partition.
# Requires: fsbl.elf, u-boot.elf, system_top.bit.bin, Image.lzma
# (produced by postimage-qspi.sh when called via post-image.sh)

SDIMGDIR="$BIN_DIR/sdimg"


echo "generating BOOT.bin"
echo "img : {[bootloader] $BIN_DIR/fsbl.elf $BIN_DIR/u-boot.elf}" > "$SDIMGDIR/boot.bif"
"$BOOTGEN" -image "$SDIMGDIR/boot.bif" -w -o i "$SDIMGDIR/BOOT.bin"

if [ -e "$BOARD_DIR/bitstream/overclock/" ]; then
    mkdir -p "$SDIMGDIR/overclock"
    for filename in "$BOARD_DIR/bitstream/overclock/"*.elf; do
        echo "img : {[bootloader] $filename $BIN_DIR/u-boot.elf}" > "$SDIMGDIR/boot.bif"
        NAME=$(basename -- "$filename" .elf)
        "$BOOTGEN" -image "$SDIMGDIR/boot.bif" -w -o i "$SDIMGDIR/overclock/BOOT_${NAME}"
    done
fi

rm "$SDIMGDIR/boot.bif"

cp "$BIN_DIR/system_top.bit.bin" "$SDIMGDIR/system_top.bin"

# Every .xsa in the board's bitstream/ folder also gets its own <name>_top.bin,
# named after the project directory that produced it (tezuka.xsa -> tezuka_top.bin,
# orf.xsa -> orf_top.bin, ...) - maia-sdr's per-project Makefile now names its
# install output that way instead of always "tezuka.xsa", so a board's bitstream/
# can carry more than one candidate bitstream side by side. system_top.bin above is
# untouched and stays what U-Boot's $bitstream_image env var defaults to; these are
# additional and selected manually with `setenv bitstream_image <name>_top.bin` at
# the U-Boot prompt, so no boot-script change is needed to use one.
for xsa in "$BOARD_DIR/bitstream/"*.xsa; do
    [ -e "$xsa" ] || continue
    NAME=$(basename -- "$xsa" .xsa)
    # Extract straight to $NAME.bit via stdout, not via a fixed system_top.bit
    # intermediate then a rename - a .xsa named system_top.xsa (one board's bitstream/
    # folder carries a stale one) would rename that file onto itself and abort under
    # set -e (mv exits 1 for "same file").
    unzip -p "$xsa" system_top.bit > "$BIN_DIR/$NAME.bit"
    echo "img : {$BIN_DIR/$NAME.bit }" > "$BIN_DIR/$NAME.bif"
    "$BOOTGEN" -image "$BIN_DIR/$NAME.bif" -process_bitstream bin -arch zynq -w -o i "$BIN_DIR/$NAME.bit.bin"
    cp "$BIN_DIR/$NAME.bit.bin" "$SDIMGDIR/${NAME}_top.bin"
    rm -f "$BIN_DIR/$NAME.bif" "$BIN_DIR/$NAME.bit" "$BIN_DIR/$NAME.bit.bin"
done

# uboot does not decompress the ramdisk — kernel handles it
mkimage -A arm -T ramdisk -C none -d "$BIN_DIR/rootfs.cpio.xz" "$SDIMGDIR/uramdisk.image.xz"
mkimage -A arm -O linux -T kernel -C lzma -a 0x8000 -e 0x8000 \
	-n "Linux kernel" -d "$BIN_DIR/Image.lzma" "$SDIMGDIR/uImage"

cp "$BIN_DIR/$DTB_NAME" "$SDIMGDIR/devicetree.dtb"
cp "$BIN_DIR/uboot-env.txt" "$SDIMGDIR/uEnv.txt"


