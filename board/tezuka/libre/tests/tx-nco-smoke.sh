#!/bin/sh
# Hardware-free integration test. Run with sh from the repository root.
set -eu
script=$(pwd)/board/tezuka/libre/overlay/usr/sbin/libresdr-tx-nco
/bin/sh "$script" self-test
tmp=$(mktemp -d "${TMPDIR:-/tmp}/tx-nco-test.XXXXXX")
export NCO_CONFIG=$tmp/config NCO_RUN_DIR=$tmp/run NCO_IIO_ROOT=$tmp/iio NCO_DEVMEM=$tmp/devmem NCO_MOCK=$tmp
mkdir -p "$tmp/iio/iio:device0" "$tmp/regs"
cleanup() {
    /bin/sh "$script" stop || :
    # Remove only the known files/directories created by this test.
    rm -f "$tmp/regs/"* "$tmp/iio/iio:device0/"* "$tmp/run/"*.tmp "$tmp/run/status" "$tmp/run/events" "$tmp/run/daemon.log"
    rm -f "$tmp/config" "$tmp/devmem" "$tmp/writes"
    rmdir "$tmp/regs" "$tmp/iio/iio:device0" "$tmp/iio" "$tmp/run" "$tmp" 2>/dev/null || :
}
trap cleanup EXIT
cat > "$tmp/config" <<'EOF'
FPGA_NCO_SUPPORTED=1
SAMPLE_SECONDS=1
MIN_SAMPLES=2
FILTER_SAMPLES=3
THRESHOLD_HZ=2
EOF
cat > "$tmp/devmem" <<'EOF'
#!/bin/sh
offset=$(($1 - 0x43c00000))
if [ "$#" -eq 3 ]; then
    printf '%s\n' "$3" > "$NCO_MOCK/regs/$offset"
    printf '%s %s\n' "$offset" "$3" >> "$NCO_MOCK/writes"
else
    cat "$NCO_MOCK/regs/$offset"
fi
EOF
chmod +x "$tmp/devmem"
phy=$tmp/iio/iio:device0
echo ad9361-phy > "$phy/name"
echo 40000000 > "$phy/xo_correction"
echo 100000000 > "$phy/out_altvoltage1_TX_LO_frequency"
echo 30720000 > "$phy/out_voltage_sampling_frequency"
for off in 0 4 12 32 36 40; do echo 0 > "$tmp/regs/$off"; done
echo 2 > "$tmp/regs/16"
echo 4294967163 > "$tmp/regs/24" # -133 Hz
/bin/sh "$script" start
wait_for() {
    tries=0
    until grep -q "$1" "$tmp/run/status" 2>/dev/null; do
        tries=$((tries + 1))
        if [ "$tries" -gt 15 ]; then cat "$tmp/run/status"; exit 1; fi
        sleep 1
    done
}
wait_for 'STABLE'
expected=$(awk 'BEGIN {printf "%.0f",int((332.5*4294967296/(30720000*(40000000-133)/40000000))+0.5)}')
[ "$(cat "$tmp/regs/36")" = "$expected" ]
# Initial apply must be RX shadow, TX shadow, control; no phase reset.
tail -n 3 "$tmp/writes" | awk 'NR==1 && $1!=32 {exit 1} NR==2 && $1!=36 {exit 1} NR==3 && ($1!=40 || $2!=6) {exit 1}'
echo 200000000 > "$phy/out_altvoltage1_TX_LO_frequency"
sleep 3
[ "$(cat "$tmp/regs/40")" = 2 ]
echo 0 > "$tmp/regs/16"
wait_for HOLDOVER
held=$(cat "$tmp/regs/36")
echo 4294967000 > "$tmp/regs/24"
sleep 2
[ "$(cat "$tmp/regs/36")" = "$held" ]
# A retune in holdover scales the retained oscillator estimate.
echo 100000000 > "$phy/out_altvoltage1_TX_LO_frequency"
sleep 2
[ "$(cat "$tmp/regs/36")" = "$expected" ]
# Converter rate changes must rescale FTW without changing the oscillator estimate.
echo 15360000 > "$phy/out_voltage_sampling_frequency"
sleep 3
double=$(awk 'BEGIN {printf "%.0f",int((332.5*4294967296/(15360000*(40000000-133)/40000000))+0.5)}')
[ "$(cat "$tmp/regs/36")" = "$double" ]
echo 30720000 > "$phy/out_voltage_sampling_frequency"
sleep 3
# Zero is a valid measurement after reacquiring fresh windows.
echo 0 > "$tmp/regs/24"
echo 2 > "$tmp/regs/16"
wait_for STABLE
[ "$(cat "$tmp/regs/36")" = 0 ]
/bin/sh "$script" stop
[ "$(cat "$tmp/regs/40")" = 2 ]
/bin/sh "$script" disable
[ "$(cat "$tmp/regs/40")" = 4 ]
[ "$(cat "$phy/xo_correction")" = 40000000 ]
[ "$(cat "$phy/out_altvoltage1_TX_LO_frequency")" = 100000000 ]
# Restart with positive error: TX correction must be negative (unsigned register encoding).
echo 133 > "$tmp/regs/24"
/bin/sh "$script" start
sleep 3
wait_for STABLE
positive=$(awk 'BEGIN {printf "%.0f",4294967296-int((332.5*4294967296/(30720000*(40000000+133)/40000000))+0.5)}')
[ "$(cat "$tmp/regs/36")" = "$positive" ]
echo 39999900 > "$phy/xo_correction"
wait_for FAULT
held=$(cat "$tmp/regs/36")
echo 300000000 > "$phy/out_altvoltage1_TX_LO_frequency"
sleep 2
[ "$(cat "$tmp/regs/36")" = "$held" ]
/bin/sh "$script" stop
writes=$(wc -l < "$tmp/writes")
if /bin/sh "$script" start; then echo 'Unexpected startup with nonnominal XO'; exit 1; fi
[ "$(wc -l < "$tmp/writes")" = "$writes" ]
echo 'PASS: signed errors, FTW formula, apply toggle/order, LO/rate changes, holdover, recovery, zero, stop/disable and XO guard.'
