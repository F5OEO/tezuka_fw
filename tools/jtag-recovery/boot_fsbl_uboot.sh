#!/bin/bash
# Load FSBL then U-Boot over JTAG via a fresh OpenOCD session.
#
# IMPORTANT: power-cycle the board before every run. FSBL relies on caches/MMU
# being disabled at reset; resuming FSBL on top of leftover state from a
# previous JTAG session reliably crashes it with an MMU translation fault.
#
# Usage: ./boot_fsbl_uboot.sh [fsbl.elf] [u-boot.elf]

set -e

FSBL="${1:-fsbl.elf}"
UBOOT="${2:-u-boot.elf}"

# Clear out any stale OpenOCD left holding the JTAG adapter from a previous
# (e.g. crashed) run, so the adapter open below doesn't fail with "busy".
sudo pkill -f "openocd -f tezuka.cfg" 2>/dev/null || true
sleep 1

sudo openocd -f tezuka.cfg \
    -c "init" \
    -c "targets zynq.cpu0" \
    -c "halt" \
    -c "load_image $FSBL 0x0 elf" \
    -c "resume 0" \
    -c "sleep 6000" \
    -c "halt" \
    -c "reg pc" \
    -c "load_image $UBOOT" \
    -c "resume 0x04000000" \
    -c "sleep 2000" \
    -c "shutdown"
