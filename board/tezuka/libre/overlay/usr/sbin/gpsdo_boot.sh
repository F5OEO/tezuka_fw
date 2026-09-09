#!/bin/sh
#
# Copyright (C) 2026 Christos Nikolaou (SV1EIA)
# Christos Nikolaou can be reached by email at : sv1eia@gmail.com
#
# gpsdo_boot.sh - LibreSDR (tezuka firmware, vctcxo_lock IP with the
# dacxx11_spi timing fix): put the VCTCXO on the external 10 MHz reference
# within seconds of boot. Runs once, in the background, from the boot hook.
#
#   1. selects the 10 MHz input; if the loop is already locked within FAST_OK
#      counts (for example after a warm restart of the script) there is nothing to do;
#   2. otherwise holds the DAC in manual mode at the best known code and waits
#      until the reference is present and stable (a GPSDO may still be warming
#      up; an unconnected input chatters);
#   3. acquires the exact code by a secant search on the linear tune curve:
#      read freq_error (one count = 1 Hz at 40 MHz = 0.025 ppm), move the code
#      by -CODES_PER_COUNT * error, repeat until |error| <= 1 (2-3 steps, ~8 s);
#   4. hands over to the PI loop so that the DAC lands on that code at once:
#      center_dac = code + old_center - live_dac compensates the loop's
#      integrator, which cannot be cleared at runtime. The loop tracks drift
#      from there. Nothing is stored; every boot re-acquires.
#
# Log: /tmp/gpsdo_boot.log and syslog (tag gpsdo). Registers: GPSDO_plan.md
# Section 4. Tunables may be overridden in /etc/default/gpsdo (firmware) or
# /boot/gpsdo/gpsdo.conf (SD).

CENTER_DEFAULT=35570   # start/hold code when nothing better is known (this unit, 2026-09-08)
CODES_PER_COUNT=52     # tune-curve slope: DAC codes per count (0.30777 counts per 12-bit code)
FAST_OK=2              # counts: already locked within this -> nothing to do
GATE_MAX_ERR=2000      # counts (50 ppm): a larger reading means no usable reference
GATE_STABLE=20         # counts: largest change allowed between two consecutive windows
SETTLE=2.5             # seconds between a DAC write and reading freq_error (> one 1 s window)
MAX_ITER=6             # secant steps before giving up (normally 2-3 are needed)
WAIT_REF=10            # seconds between reference checks while waiting
XO_NOMINAL=40000000    # xo_correction while the reference steers the crystal

[ -f /etc/default/gpsdo ] && . /etc/default/gpsdo
[ -f /boot/gpsdo/gpsdo.conf ] && . /boot/gpsdo/gpsdo.conf

R_CTRL=0x43C00000; R_SET=0x43C00004; R_DAC=0x43C00008; R_REF=0x43C0000C
R_ST=0x43C00010; R_ERR=0x43C00018
CTRL_LOOP=0x0001F1F0; CTRL_MANUAL=0x0001F1F1
XO_SYSFS=/sys/bus/iio/devices/iio:device0/xo_correction

log() { echo "$(date +%T) $*" >> /tmp/gpsdo_boot.log; logger -t gpsdo "$*" 2>/dev/null; }
rd() { devmem "$1" 32; }
wr() { devmem "$1" 32 "$2"; }
abs() { echo "${1#-}"; }
err() { v=$(( $(rd $R_ERR) )); [ "$v" -ge 2147483648 ] && v=$((v - 4294967296)); echo "$v"; }
dac() { echo $(( $(rd $R_DAC) & 0xFFFF )); }
center() { echo $(( ($(rd $R_SET) >> 16) & 0xFFFF )); }
present() { echo $(( ($(rd $R_ST) >> 1) & 1 )); }
locked() { echo $(( $(rd $R_ST) & 1 )); }
manual_mode() { echo $(( $(rd $R_CTRL) & 1 )); }
clamp() { v=$1; [ "$v" -lt 0 ] && v=0; [ "$v" -gt 65535 ] && v=65535; echo "$v"; }

# manual mode, DAC at $1, center register kept
set_manual() { wr $R_SET "$(printf '0x%04X%04X' "$(center)" "$1")"; wr $R_CTRL $CTRL_MANUAL; }

# loop mode with the DAC landing on $1 (integrator compensation)
set_loop() {
    wr $R_CTRL $CTRL_LOOP
    d=$(dac); c=$(center); n=$1
    if [ "$d" -gt 0 ] && [ "$d" -lt 65535 ]; then n=$(( $1 + c - d )); fi
    n=$(clamp "$n")
    wr $R_SET "$(printf '0x%04X0000' "$n")"
}

# reference present, readings sane and stable over two windows
reference_ok() {
    [ "$(present)" -eq 1 ] || return 1
    e1=$(err); sleep 1.2; e2=$(err)
    [ "$(abs "$e1")" -lt "$GATE_MAX_ERR" ] || return 1
    [ "$(abs "$e2")" -lt "$GATE_MAX_ERR" ] || return 1
    [ "$(abs $((e1 - e2)))" -le "$GATE_STABLE" ]
}

: > /tmp/gpsdo_boot.log
log "start (default $CENTER_DEFAULT, $CODES_PER_COUNT codes/count)"
[ -w "$XO_SYSFS" ] && echo "$XO_NOMINAL" > "$XO_SYSFS"
wr $R_REF 0x00000000

# fast path: already disciplining within FAST_OK counts
if [ "$(manual_mode)" -eq 0 ] && [ "$(locked)" -eq 1 ]; then
    e=$(err)
    if [ "$(abs "$e")" -le "$FAST_OK" ]; then log "already locked, err $e, dac $(dac): nothing to do"; exit 0; fi
fi

# best starting code: the live DAC if the loop was running and not railed
code=$CENTER_DEFAULT
if [ "$(manual_mode)" -eq 0 ]; then d=$(dac); [ "$d" -gt 0 ] && [ "$d" -lt 65535 ] && code=$d; fi
set_manual "$code"
log "manual hold at $code, waiting for a usable reference"
until reference_ok; do sleep "$WAIT_REF"; done
log "reference present and stable, acquiring"

i=0
while [ "$i" -lt "$MAX_ITER" ]; do
    set_manual "$code"; sleep "$SETTLE"; e=$(err)
    log "iter $i: code $code err $e"
    [ "$(abs "$e")" -le 1 ] && break
    code=$(clamp $(( code - CODES_PER_COUNT * e )))
    i=$((i + 1))
done
set_loop "$code"
sleep 3
log "loop on: target $code, dac $(dac), err $(err), locked $(locked), center register $(center)"
exit 0
