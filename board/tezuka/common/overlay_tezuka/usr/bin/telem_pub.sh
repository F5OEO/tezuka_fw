#!/bin/sh
# OpenRF Maia telemetry publisher: read AD9361 RSSI + chip temp and the Zynq XADC (SoC) temp from iio sysfs,
# publish them (retained) to MQTT so the OpenRF MaiaSource plugin can show them (maia-httpd exposes none of these).
# Mirrors PlutoSource: rssi reported negated (dBFS), AD9361 temp = input/1000, XADC temp = (raw+offset)*scale/1000.
phy=""; xadc=""
for d in /sys/bus/iio/devices/iio:device*; do
  n=$(cat "$d/name" 2>/dev/null)
  [ "$n" = "ad9361-phy" ] && phy="$d"
  [ "$n" = "xadc" ] && xadc="$d"
done
while true; do
  rssi=$(awk '{printf "%.1f", -$1}' "$phy/in_voltage0_rssi" 2>/dev/null)
  temp=$(awk '{printf "%.1f", $1/1000}' "$phy/in_temp0_input" 2>/dev/null)
  raw=$(cat "$xadc/in_temp0_raw" 2>/dev/null)
  sc=$(cat "$xadc/in_temp0_scale" 2>/dev/null)
  off=$(cat "$xadc/in_temp0_offset" 2>/dev/null)
  cpu=$(awk -v r="$raw" -v s="$sc" -v o="$off" 'BEGIN{ if (s != "") printf "%.1f", (r+o)*s/1000 }')
  mosquitto_pub -h localhost -t state/telem -r -m "rssi=$rssi temp=$temp cpu=$cpu" 2>/dev/null
  sleep 2
done
