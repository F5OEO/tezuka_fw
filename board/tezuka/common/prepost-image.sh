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