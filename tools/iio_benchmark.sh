#!/bin/sh
# IIO Benchmark Script for Tezuka Firmware
# Runs on-device (BusyBox sh compatible)
#
# Usage: sh iio_benchmark.sh [label]
#   label: optional experiment name (e.g. "tier1-hardening")
#
# Output: plain text to stdout. Redirect to save:
#   sh iio_benchmark.sh "baseline" > /tmp/benchmark.txt

set -e

LABEL="${1:-unnamed}"
URI="local:"
RX_DEV="cf-ad9361-lpc"
PHY_DEV="ad9361-phy"
SWEEP_RATE=30720000
SWEEP_BUFSZ=65536
STREAM_DURATION=10
STRESS_DURATION=60
STRESS_THREADS=2

# helpers — iio_attr output format: "... value: 'xxx'" or "... value 'xxx'"
iio_read_dev_attr() {
    iio_attr -u "$URI" -d "$PHY_DEV" "$1" 2>/dev/null | sed "s/.*value[: ]*'//" | sed "s/'$//"
}

iio_read_chan_attr() {
    # -i = input channel; $1=channel $2=attr
    iio_attr -u "$URI" -i -c "$PHY_DEV" "$1" "$2" 2>/dev/null | sed "s/.*value[: ]*'//" | sed "s/'$//"
}

iio_set_chan_attr() {
    # -i = input channel; $1=channel $2=attr $3=value
    iio_attr -u "$URI" -i -c "$PHY_DEV" "$1" "$2" "$3" >/dev/null 2>&1
}

set_sample_rate() {
    iio_set_chan_attr voltage0 sampling_frequency "$1"
}

get_sample_rate() {
    iio_read_chan_attr voltage0 sampling_frequency
}

run_readdev_bench() {
    _bs="$1"
    # iio_readdev -B prints throughput to stderr with ANSI escape sequences
    # strip escapes, take last non-empty throughput value
    timeout "$STREAM_DURATION" iio_readdev -u "$URI" -b "$_bs" -B "$RX_DEV" \
        >/dev/null 2>/tmp/iio_bench_out || true
    sed 's/\x1b\[[0-9]*K//g' /tmp/iio_bench_out 2>/dev/null \
        | tr '\r' '\n' | grep -i throughput | tail -1 || echo "ERROR"
}

kconfig_check() {
    if [ -f /proc/config.gz ]; then
        zcat /proc/config.gz 2>/dev/null | grep -w "$1" | head -1
    else
        echo "n/a"
    fi
}

# ===================================================================
echo "=== IIO BENCHMARK ==="
echo "label:    $LABEL"
echo "date:     $(date -Iseconds 2>/dev/null || date)"
echo "host:     $(hostname)"
echo "uptime:   $(cut -d' ' -f1 /proc/uptime)s"
echo ""

# ===================================================================
echo "=== SYSTEM INFO ==="
echo "kernel:   $(uname -r)"
echo "arch:     $(uname -m)"
echo "cmdline:  $(cat /proc/cmdline)"
echo ""

# ===================================================================
echo "=== KERNEL CONFIG ==="
for k in CONFIG_HZ CONFIG_PREEMPT CONFIG_PREEMPT_RT \
         CONFIG_STACKPROTECTOR_STRONG CONFIG_FORTIFY_SOURCE \
         CONFIG_STRICT_KERNEL_RWX CONFIG_STRICT_MODULE_RWX \
         CONFIG_SLUB CONFIG_SLAB CONFIG_HW_RANDOM; do
    val=$(kconfig_check "$k")
    if [ -n "$val" ]; then
        echo "  $val"
    fi
done
echo ""

# ===================================================================
echo "=== IIO DEVICE INFO ==="
echo "phy:           $PHY_DEV"
echo "rx_dev:        $RX_DEV"
echo "sample_rate:   $(get_sample_rate)"
echo "rx_path_rates: $(iio_read_dev_attr rx_path_rates)"
echo "tx_path_rates: $(iio_read_dev_attr tx_path_rates)"
echo "temperature:   $(iio_read_chan_attr temp0 input)"
echo ""

# ===================================================================
echo "=== MODULES ==="
lsmod
echo ""

# ===================================================================
echo "=== FPGA OVERFLOW REGISTERS (pre-test) ==="
for addr in 0x79020400 0x79020440 0x79020480 0x790204C0; do
    val=$(busybox devmem "$addr" 32 2>/dev/null || echo "n/a")
    echo "  $addr = $val"
done
echo ""

# ===================================================================
echo "=== BUFFER SIZE SWEEP (sample_rate=$SWEEP_RATE) ==="
set_sample_rate "$SWEEP_RATE"
actual=$(get_sample_rate)
echo "actual_rate: $actual"
echo ""
printf "%-12s  %s\n" "buffer_size" "throughput"
printf "%-12s  %s\n" "-----------" "----------"
for bs in 256 1024 4096 16384 65536 262144; do
    result=$(run_readdev_bench "$bs")
    printf "%-12s  %s\n" "$bs" "$result"
done
echo ""

# ===================================================================
echo "=== SAMPLE RATE SWEEP (buffer_size=$SWEEP_BUFSZ) ==="
printf "%-14s  %-14s  %s\n" "requested" "actual" "throughput"
printf "%-14s  %-14s  %s\n" "---------" "------" "----------"
for sr in 2500000 10000000 20000000 30720000 40000000 61440000; do
    set_sample_rate "$sr"
    actual=$(get_sample_rate)
    result=$(run_readdev_bench "$SWEEP_BUFSZ")
    printf "%-14s  %-14s  %s\n" "$sr" "$actual" "$result"
done
echo ""

# ===================================================================
echo "=== FPGA OVERFLOW REGISTERS (post-test) ==="
for addr in 0x79020400 0x79020440 0x79020480 0x790204C0; do
    val=$(busybox devmem "$addr" 32 2>/dev/null || echo "n/a")
    echo "  $addr = $val"
done
echo ""

# ===================================================================
echo "=== STRESS TEST (${STRESS_DURATION}s, ${STRESS_THREADS} threads) ==="
set_sample_rate "$SWEEP_RATE"
timeout "$((STRESS_DURATION + 5))" \
    iio_stresstest -u "$URI" -b 4096 -t "$STRESS_THREADS" \
    -T "$STRESS_DURATION" -v "$RX_DEV" 2>&1 || true
echo ""

# ===================================================================
# restore default sample rate
set_sample_rate "$SWEEP_RATE"

echo "=== DONE ==="
echo "label: $LABEL"
echo "date:  $(date -Iseconds 2>/dev/null || date)"

# cleanup
rm -f /tmp/iio_bench_out
