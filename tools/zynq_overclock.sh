#!/bin/sh
# Overclock (or underclock) the Zynq-7000 ARM PLL from Linux by writing SLCR registers.
# Runs ON THE TARGET (needs busybox devmem, root). Not persistent: lost at reboot.
#
#   zynq_overclock.sh              show current ARM PLL / CPU frequency
#   zynq_overclock.sh <cpu_mhz>    set CPU clock, e.g. 800 (default boot value 667)
#
# Env: PS_CLK_MHZ  PS_CLK input (default 33.333333)
#      PLL_CFG     ARM_PLL_CFG value to write (default: keep the current one, which
#                  the boot ps7_init set for FDIV=40, valid for high FDIV values)
#      FORCE=1     allow PLL above 1800 MHz
#
# Registers (UG585, SLCR base 0xF8000000):
#   0x004 SLCR_LOCK (0x767B)   0x008 SLCR_UNLOCK (0xDF0D)
#   0x100 ARM_PLL_CTRL: [0] RESET [4] BYPASS_FORCE [18:12] FDIV
#   0x10C PLL_STATUS:   [0] ARM_PLL_LOCK
#   0x110 ARM_PLL_CFG:  [7:4] PLL_RES [11:8] PLL_CP [21:12] LOCK_CNT
#   0x120 ARM_CLK_CTRL: [13:8] DIVISOR  (CPU = ARM PLL / DIVISOR)

SLCR=0xF8000000
reg() { printf '0x%08X' $((SLCR + $1)); }
rd()  { devmem "$(reg $1)" 32; }
wr()  { devmem "$(reg $1)" 32 "$2"; }

[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
command -v devmem >/dev/null || { echo "devmem not found" >&2; exit 1; }

REF_KHZ=$(awk -v m="${PS_CLK_MHZ:-33.333333}" 'BEGIN{printf "%d", m*1000+0.5}')

CTRL=$(rd 0x100); CFG=$(rd 0x110); CLK=$(rd 0x120)
FDIV=$(( (CTRL >> 12) & 0x7F ))
DIV=$(( (CLK >> 8) & 0x3F ))
[ "$DIV" -gt 0 ] || DIV=2
show() { echo "ARM PLL: FDIV=$1 -> $(( REF_KHZ * $1 / 1000 )) MHz, divisor=$DIV -> CPU $(( REF_KHZ * $1 / DIV / 1000 )) MHz"; }

if [ $# -eq 0 ]; then
    show "$FDIV"
    exit 0
fi

CPU_MHZ=$1
case $CPU_MHZ in ''|*[!0-9]*) echo "cpu_mhz must be an integer" >&2; exit 1;; esac
NEW=$(( (CPU_MHZ * 1000 * DIV + REF_KHZ / 2) / REF_KHZ ))
PLL_MHZ=$(( REF_KHZ * NEW / 1000 ))
if [ "$NEW" -lt 13 ] || [ "$NEW" -gt 127 ]; then
    echo "FDIV $NEW out of range" >&2; exit 1
fi
if [ "$PLL_MHZ" -gt 1800 ] && [ "${FORCE:-0}" != 1 ]; then
    echo "PLL would be ${PLL_MHZ} MHz (>1800), set FORCE=1 to override" >&2; exit 1
fi

echo -n "current: "; show "$FDIV"
echo -n "target:  "; show "$NEW"

# keep RES/CP/LOCK_CNT unless overridden
if [ -n "$PLL_CFG" ]; then CFG_NEW=$PLL_CFG; else CFG_NEW=$(( CFG & 0x003FFFF0 )); fi

# CPU is briefly clocked from PS_CLK (bypass) while the PLL relocks: keep it short.
wr 0x008 0xDF0D                                              # unlock SLCR
wr 0x110 "$(printf '0x%08X' $CFG_NEW)"
C=$(( (CTRL & ~0x7F000) | (NEW << 12) ))
wr 0x100 "$(printf '0x%08X' $(( C | 0x10 )))"                # new FDIV + bypass
wr 0x100 "$(printf '0x%08X' $(( C | 0x10 | 0x1 )))"          # reset PLL
wr 0x100 "$(printf '0x%08X' $(( C | 0x10 )))"                # release reset
n=0
while [ $(( $(rd 0x10C) & 1 )) -eq 0 ]; do                   # wait ARM_PLL_LOCK
    n=$((n + 1))
    if [ $n -gt 100000 ]; then
        wr 0x100 "$(printf '0x%08X' $CTRL)"; wr 0x110 "$(printf '0x%08X' $CFG)"
        wr 0x004 0x767B
        echo "PLL did not lock, restoring previous settings" >&2; exit 1
    fi
done
wr 0x100 "$(printf '0x%08X' $C)"                             # leave bypass
wr 0x004 0x767B                                              # lock SLCR

echo -n "now:     "; show "$(( ($(rd 0x100) >> 12) & 0x7F ))"
