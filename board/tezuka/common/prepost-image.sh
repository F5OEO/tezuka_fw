#!/bin/sh
set -e
COMMON_DIR="$(dirname "$0")"
BIN_DIR="$1"
# Args from BR2_ROOTFS_POST_IMAGE_SCRIPT_ARG in board config file
BOARD_DIR="$2"

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
# ── Shared: kernel, bitstream, U-Boot env ────────────────────────────────────
# These artifacts are consumed by both the flash (.frm/.dfu) and SD paths.

# Extract raw kernel from zImage and compress with lzma
skip=$(LC_ALL=C grep -a -b -o -P '\x1f\x8b\x08' "$BIN_DIR/zImage" | head -1 | cut -d: -f1)
dd if="$BIN_DIR/zImage" bs=1 skip="$skip" | gunzip > "$BIN_DIR/Image" 2>/dev/null || true

# Guard against the zImage self-decompression corruption from issue #450:
# every board's flash FIT (plutomaia.its) loads the kernel at 0x8000 and
# ships it as a self-extracting zImage (compression="none"). On real
# hardware this corrupts scattered bytes starting at Image-file offset
# 0xE88000 (phys 0x8000 + 0xE88000 = 0xE90000) on every boot, once the
# uncompressed kernel grows past it -- confirmed independent of bootloader
# version. This does not affect Image.lzma / the SD uImage path, which
# U-Boot decompresses itself.
KERNEL_CORRUPT_OFFSET=$((0xE88000))   # 15237120 bytes / ~14.53 MiB
KERNEL_WARN_MARGIN=$((512 * 1024))    # heads-up 512 KiB before the cliff
IMAGE_SIZE=$(wc -c < "$BIN_DIR/Image")
# Boards whose plutomaia.its already ships Image.lzma/compression=lzma
# (see PR #449) let U-Boot do the decompression instead of the kernel's
# own self-extracting zImage stub, so this offset never applies to them.
if grep -q '"lzma"' "$BOARD_DIR/plutomaia.its" 2>/dev/null; then
    :
elif [ "$IMAGE_SIZE" -ge "$KERNEL_CORRUPT_OFFSET" ]; then
    echo "ERROR: kernel Image is $IMAGE_SIZE bytes, at or past the known" >&2
    echo "       zImage self-decompression corruption offset 0xE88000" >&2
    echo "       (~14.53 MiB, see issue #450). Any board booting this" >&2
    echo "       kernel from flash via plutomaia.its WILL corrupt .rodata" >&2
    echo "       at boot (wrong FIR/gain/RSSI/temperature, no rates below" >&2
    echo "       2.083 MSPS). Trim the kernel or switch that board's FIT" >&2
    echo "       to Image.lzma/compression=lzma (see PR #449)." >&2
    exit 1
elif [ "$IMAGE_SIZE" -ge "$((KERNEL_CORRUPT_OFFSET - KERNEL_WARN_MARGIN))" ]; then
    echo "WARNING: kernel Image is $IMAGE_SIZE bytes, within $(( (KERNEL_CORRUPT_OFFSET - IMAGE_SIZE) / 1024 )) KiB of the zImage self-decompression corruption offset (issue #450)."
fi

lzma -z -k -f "$BIN_DIR/Image"

# Convert FPGA bitstream to raw binary
echo "img : {$BIN_DIR/system_top.bit }" > "$BIN_DIR/system.bif"
"$BOOTGEN" -image "$BIN_DIR/system.bif" -process_bitstream bin -arch zynq -w -o i "$BIN_DIR/system_top.bit.bin"

# Prepare FSBL + U-Boot ELF (reused by boot.img and BOOT.bin)
cp "$BOARD_DIR/bitstream/fsbl.elf" "$BIN_DIR"
cp "$BIN_DIR/u-boot" "$BIN_DIR/u-boot.elf"

# Generate U-Boot environment binary
FW_VERSION=$(cd "$COMMON_DIR" && git describe --abbrev=4 --always --tags)
FIT_SIZE="${4:-$(grep "^fit_size=" "$BOARD_DIR/uboot-env.txt" 2>/dev/null | cut -d= -f2)}"
: "${FIT_SIZE:=0x1E00000}"
sed -e "s/#BUILD#/${FW_VERSION}/g" \
    -e "s/^fit_size=.*/fit_size=${FIT_SIZE}/" \
    "$COMMON_DIR/uboot-env.txt" > "$BIN_DIR/uboot-env.txt"
"$HOST_DIR/bin/mkenvimage" -s 0x20000 -o "$BIN_DIR/uboot-env.bin" "$BIN_DIR/uboot-env.txt"

QSPIDIR="$BIN_DIR/flash"
mkdir -p "$QSPIDIR"

SDIMGDIR="$BIN_DIR/sdimg"
mkdir -p "$SDIMGDIR"

JTAGDIR="$QSPIDIR/jtag"
mkdir -p "$JTAGDIR"