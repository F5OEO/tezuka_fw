#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"
MQTT_FIFO="/tmp/mqtt_fifo"

folder='/sys/bus/iio/devices/iio:device0/'
echo 280000000 > /tmp/sweep_frequency
echo 480000000 > /tmp/sweep_span

# List of current values for different settings
declare -A VMAP=(
  [${folder}out_voltage0_rf_port_select]='tx/ant'
  [${folder}in_voltage0_rf_port_select]='rx/ant'
  [${folder}out_altvoltage1_TX_LO_frequency]='tx/frequency'
  [${folder}out_altvoltage0_RX_LO_frequency]='rx/frequency'
  [${folder}out_voltage_rf_bandwidth]='tx/bandwidth'
  [${folder}in_voltage_rf_bandwidth]='rx/bandwidth'
  [${folder}xo_correction]='main/freq_correction'
  [${folder}out_voltage_sampling_frequency]='tx/sampling'
  [${folder}in_voltage_sampling_frequency]='rx/sampling'
  [${folder}in_voltage0_gain_control_mode]='rx/gain_mode'
  [${folder}in_voltage0_hardwaregain]='rx/gain'
  [${folder}out_voltage0_hardwaregain]='tx/gain'
  [${folder}out_altvoltage1_TX_LO_powerdown]='tx/active'
  [${folder}out_altvoltage0_RX_LO_powerdown]='rx/active'
  [/sys/kernel/debug/iio/iio:device0/adi,1rx-1tx-mode-use-rx-num]='rx/rfinput'
  [${folder}in_voltage_filter_fir_en]='rx/fir_enable'
)

# List of possible values (capabilities/caps) for reported values settings
declare -A cVMAP=(
  [${folder}out_voltage_rf_port_select_available]='caps/tx/ant'
  [${folder}in_voltage_rf_port_select_available]='caps/rx/ant'
  [${folder}out_altvoltage1_TX_LO_frequency_available]='caps/tx/frequency'
  [${folder}out_altvoltage0_RX_LO_frequency_available]='caps/rx/frequency'
  [${folder}out_voltage_rf_bandwidth_available]='caps/tx/bandwidth'
  [${folder}in_voltage_rf_bandwidth_available]='caps/rx/bandwidth'
  [${folder}xo_correction_available]='caps/main/freq_correction'
  [${folder}out_voltage_sampling_frequency_available]='caps/tx/sampling'
  [${folder}in_voltage_sampling_frequency_available]='caps/rx/sampling'
  [${folder}in_voltage_gain_control_mode_available]='caps/rx/gain_mode'
  [${folder}in_voltage0_hardwaregain_available]='caps/rx/gain'
  [${folder}out_voltage0_hardwaregain_available]='caps/tx/gain'
)

# Reverse map for write commands
declare -A rVMAP=(
  [rx/gain]=${folder}in_voltage0_hardwaregain
)

# ============================================================
#  CACHE + PERSISTENT CONNECTIONS
# ============================================================

declare -A CACHE

# --- MQTT: FIFO-fed background publisher (one persistent process) ---
rm -f "$MQTT_FIFO"
mkfifo "$MQTT_FIFO"

# Background consumer reads "topic\tpayload" lines via --stdin-line pattern
# Single loop, no fork-per-message: mosquitto_pub is called only for
# messages that passed the cache check.
(
  while IFS=$'\t' read -r topic payload; do
    /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "$topic" -m "$payload"
  done
) < "$MQTT_FIFO" &
MQTT_PID=$!

# Keep the write-end open so the FIFO never sees EOF
exec 4>"$MQTT_FIFO"

# --- Serial: open once, reuse file descriptor ---
SERIAL_FD=""
if [ -e "${SERIAL_PORT}" ]; then
  exec 5>"${SERIAL_PORT}"
  SERIAL_FD=5
fi

# Cleanup on exit
cleanup () {
  exec 4>&- 2>/dev/null
  [ -n "$SERIAL_FD" ] && exec 5>&- 2>/dev/null
  kill "$MQTT_PID" 2>/dev/null
  rm -f "$MQTT_FIFO"
}
trap cleanup EXIT

# ============================================================
#  LOW-LEVEL PUBLISH HELPERS
# ============================================================

# Send to MQTT via the persistent FIFO (no fork)
mqtt_publish () {
  printf '%s\t%s\n' "state/${1}" "${2}" >&4
}

# Send to serial via the persistent FD (no fork)
serial_publish () {
  [ -n "$SERIAL_FD" ] && echo "${1} ${2}" >&5
}

# Publish only if the value changed (cached)
publish () {
  local key="$1" val="$2"
  if [ "${CACHE[$key]}" != "$val" ]; then
    CACHE[$key]="$val"
    mqtt_publish "$key" "$val"
    serial_publish "$key" "$val"
  fi
}

# Force-publish (bypass cache — use after a write command)
publish_force () {
  CACHE[$1]="$2"
  mqtt_publish "$1" "$2"
  serial_publish "$1" "$2"
}

# ============================================================
#  FILE READING (bash built-in, no fork)
# ============================================================

read_file () {
  if [ -r "$1" ]; then
    echo "$(<"$1")"
  else
    echo 'n/a'
  fi
}

# ============================================================
#  DATA PUBLISHING
# ============================================================

# Poll all data every cycle — cache prevents redundant MQTT/serial writes
dump_data () {
  # IIO settings (VMAP)
  for K in "${!VMAP[@]}"; do publish "${VMAP[$K]}" "$(read_file "$K")"; done

  # Capabilities (cVMAP)
  for K in "${!cVMAP[@]}"; do publish "${cVMAP[$K]}" "$(read_file "$K")"; done

  # Device info
  publish "main/serial"     "$(read_file /etc/serial)"
  publish "main/hw_model"   "$(grep 'hw_model='   /etc/libiio.ini | sed 's,hw_model=,,')"
  publish "main/fw_version" "$(grep 'fw_version=' /etc/libiio.ini | sed 's,fw_version=,,')"

  # Sweep state
  sweep=$(iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable)
  if [ "$sweep" == "1" ]; then
    publish "rx/sweep/activate" "1"
  else
    publish "rx/sweep/activate" "0"
  fi
  echo "$sweep" > /tmp/sweep_on

  sweep_frequency=$(< /tmp/sweep_frequency)
  sweep_span=$(< /tmp/sweep_span)
  publish "rx/sweep/frequency" "$sweep_frequency"
  publish "rx/span" "$sweep_span"

  # RX overload flag
  rx_overload=$(iio_attr -D ad9361-phy direct_reg_access 0x5E)
  rx_overload=$((rx_overload & 1))
  if [ "$rx_overload" == "1" ]; then
    publish "rx/overload" "1"
  else
    publish "rx/overload" "0"
  fi
}

# ============================================================
#  SWEEP HELPER
# ============================================================

update_sweep () {
  /root/sweep.sh "$1" "$2" "$3"
}

# ============================================================
#  COMMAND PARSER (from MQTT cmd/# topic)
# ============================================================

parse_cmd () {
  cmd="${1//cmd\//}"
  shift
  val="${*}"

  # Fast path: direct sysfs write via reverse map
  if [ "${rVMAP[$cmd]}" ]; then
    echo "${val}" > "${rVMAP[$cmd]}" &
  else
    case $cmd in
    rx/rfinput)
      /root/switch_rfinput.sh "${val}" &
    ;;
    rx/span)
      SPAN=$(printf "%0.f" "$val")
      echo "$SPAN" > /tmp/sweep_span
      if [ "$SPAN" -gt "60000000" ]; then
        current_frequency=$(< "${folder}out_altvoltage0_RX_LO_frequency")
        update_sweep "$current_frequency" "$SPAN" 1
      else
        /root/sweep_stop.sh
        echo 60000000 > "${folder}in_voltage_sampling_frequency"
      fi
    ;;
    rx/frequency)
      SPAN=$(< /tmp/sweep_span)
      sweep_on=$(< /tmp/sweep_on)
      echo "$val" > /tmp/sweep_frequency
      if [ "$sweep_on" == "1" ]; then
        update_sweep "$val" "$SPAN" 1
      else
        echo "$val" > "${folder}out_altvoltage0_RX_LO_frequency"
      fi
    ;;
    rx/sweep/frequency)
      if [ "$sweep_frequency" != "$val" ]; then
        sweep_frequency=$val
        echo "$sweep_frequency" > /tmp/sweep_frequency
        update_sweep "$sweep_frequency" "$sweep_span" "$sweep_on"
      fi
    ;;
    rx/sweep/span)
      if [ "$sweep_span" != "$val" ]; then
        sweep_span=$val
        echo "$sweep_span" > /tmp/sweep_span
        update_sweep "$sweep_frequency" "$sweep_span" "$sweep_on"
      fi
    ;;
    rx/sweep/activate)
      sweep_on=$(< /tmp/sweep_on)
      if [ "$val" == "1" ]; then
        sweep_on=$val
        update_sweep "$sweep_frequency" "$sweep_span" "$sweep_on"
      else
        /root/sweep_stop.sh
      fi
    ;;
    esac
  fi
}

# ============================================================
#  MAIN
# ============================================================

# Subscribe to commands (background)
/usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "cmd/#" | while read -r i; do parse_cmd $i ; done &

# Poll dynamic data every 2 seconds (cache prevents redundant publishes)
while true; do
  dump_data
  sleep 2
done
