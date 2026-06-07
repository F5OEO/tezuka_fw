#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"
MQTT_FIFO="/tmp/mqtt_fifo"

folder=$(grep -rl 'ad9361-phy' /sys/bus/iio/devices/*/name 2>/dev/null | head -1 | xargs dirname)/
if [ -z "$folder" ] || [ "$folder" = "/" ]; then
  echo "ERROR: Could not find IIO device with name 'ad9361-phy'" >&2
  exit 1
fi

xadc_folder=$(grep -rl 'xadc' /sys/bus/iio/devices/*/name 2>/dev/null | head -1 | xargs dirname 2>/dev/null)/
dds_folder=$(grep -rl 'cf-ad9361-dds-core-lpc' /sys/bus/iio/devices/*/name 2>/dev/null | head -1 | xargs dirname 2>/dev/null)/
adc_folder=$(grep -rl 'cf-ad9361-lpc'         /sys/bus/iio/devices/*/name 2>/dev/null | head -1 | xargs dirname 2>/dev/null)/
debug_folder="/sys/kernel/debug/iio/$(basename "${folder%/}")/"

# Static version strings — read once at startup, never change at runtime
IIO_VERSION=$(iio_info --version 2>/dev/null | awk 'NR==1{print $3; exit}')

# Base XO oscillator frequency — captured before any correction is applied
XO_BASE=$(<"${folder}xo_correction" 2>/dev/null)
XO_BASE=${XO_BASE:-40000000}

echo 280000000 > /tmp/sweep_frequency
echo 480000000 > /tmp/sweep_span

# List of current values for different settings
declare -A VMAP=(
  [${folder}out_voltage0_rf_port_select]='tx/ant'
  [${folder}in_voltage0_rf_port_select]='rx/ant'
  [${folder}out_altvoltage1_TX_LO_frequency]='tx/frequency'
  [${folder}out_altvoltage0_RX_LO_frequency]='rx/frequency'
  [${folder}out_altvoltage1_TX_LO_powerdown]='tx/active'
  [${folder}out_altvoltage0_RX_LO_powerdown]='rx/active'
  [${folder}out_voltage_rf_bandwidth]='tx/bandwidth'
  [${folder}in_voltage_rf_bandwidth]='rx/bandwidth'
  [${folder}out_voltage_sampling_frequency]='tx/sampling'
  [${folder}in_voltage_sampling_frequency]='rx/sampling'
  [${folder}in_voltage0_gain_control_mode]='rx/gain_mode'
  [${folder}in_voltage0_hardwaregain]='rx/gain'
  [${folder}out_voltage0_hardwaregain]='tx/gain'
  [${folder}in_voltage0_rssi]='rx/rssi'
  [${folder}in_voltage0_bb_dc_offset_tracking_en]='rx/bb_dc_tracking'
  [${folder}in_voltage0_quadrature_tracking_en]='rx/quad_tracking'
  [${folder}in_voltage0_rf_dc_offset_tracking_en]='rx/rf_dc_tracking'
  [${folder}in_voltage_filter_fir_en]='rx/fir_enable'
  [${folder}xo_correction]='main/freq_correction'
  [${folder}ensm_mode]='main/ensm_mode'
  [${folder}rx_path_rates]='main/rx_path_rates'
  [${folder}tx_path_rates]='main/tx_path_rates'
  [${debug_folder}adi,1rx-1tx-mode-use-rx-num]='rx/rfinput'
  [${debug_folder}adi,1rx-1tx-mode-use-tx-num]='tx/rfinput'
  [${debug_folder}loopback]='rx/loopback'
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
  [${folder}ensm_mode_available]='caps/main/ensm_mode'
)

# Reverse map: cmd/<path> → sysfs file to write
declare -A rVMAP=(
  [rx/gain]=${folder}in_voltage0_hardwaregain
  [tx/gain]=${folder}out_voltage0_hardwaregain
  [rx/gain_mode]=${folder}in_voltage0_gain_control_mode
  [tx/frequency]=${folder}out_altvoltage1_TX_LO_frequency
  [rx/sampling]=${folder}in_voltage_sampling_frequency
  [tx/sampling]=${folder}out_voltage_sampling_frequency
  [rx/bandwidth]=${folder}in_voltage_rf_bandwidth
  [tx/bandwidth]=${folder}out_voltage_rf_bandwidth
  [rx/ant]=${folder}in_voltage0_rf_port_select
  [tx/ant]=${folder}out_voltage0_rf_port_select
  [rx/active]=${folder}out_altvoltage0_RX_LO_powerdown
  [tx/active]=${folder}out_altvoltage1_TX_LO_powerdown
  [rx/fir_enable]=${folder}in_voltage_filter_fir_en
  [rx/bb_dc_tracking]=${folder}in_voltage0_bb_dc_offset_tracking_en
  [rx/quad_tracking]=${folder}in_voltage0_quadrature_tracking_en
  [rx/rf_dc_tracking]=${folder}in_voltage0_rf_dc_offset_tracking_en
  [main/freq_correction]=${folder}xo_correction
  [main/ensm_mode]=${folder}ensm_mode
  [siggen/frequency]=${dds_folder}out_altvoltage0_TX1_I_F1_frequency
  [siggen/scale]=${dds_folder}out_altvoltage0_TX1_I_F1_scale
  [siggen/enable]=${dds_folder}out_altvoltage0_TX1_I_F1_raw
)

# ============================================================
#  CACHE + PERSISTENT CONNECTIONS
# ============================================================

declare -A CACHE

rm -f "$MQTT_FIFO"
mkfifo "$MQTT_FIFO"

(
  while IFS=$'\t' read -r topic payload; do
    /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "$topic" -m "$payload"
  done
) < "$MQTT_FIFO" &
MQTT_PID=$!

exec 4>"$MQTT_FIFO"

SERIAL_FD=""
if [ -e "${SERIAL_PORT}" ]; then
  exec 5>"${SERIAL_PORT}"
  SERIAL_FD=5
fi

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

mqtt_publish () {
  printf '%s\t%s\n' "state/${1}" "${2}" >&4
}

serial_publish () {
  [ -n "$SERIAL_FD" ] && echo "${1} ${2}" >&5
}

publish () {
  local key="$1" val="$2"
  mqtt_publish "$key" "$val"
  if [ "${CACHE[$key]}" != "$val" ]; then
    CACHE[$key]="$val"
    serial_publish "$key" "$val"
  fi
}

publish_force () {
  mqtt_publish "$1" "$2"
  CACHE[$1]="$2"
  serial_publish "$1" "$2"
}

read_file () {
  if [ -r "$1" ]; then
    echo "$(<"$1")"
  else
    echo 'n/a'
  fi
}

prefix_to_mask () {
  local p=$1 i octet m=""
  for i in 1 2 3 4; do
    local b=$(( p > 8 ? 8 : p < 0 ? 0 : p ))
    octet=$(( b == 0 ? 0 : 256 - (1 << (8 - b)) ))
    m="${m:+$m.}$octet"
    p=$(( p - b ))
  done
  echo "$m"
}

# ============================================================
#  DATA PUBLISHING
# ============================================================

dump_data () {
  for K in "${!VMAP[@]}"; do publish "${VMAP[$K]}" "$(read_file "$K")"; done
  for K in "${!cVMAP[@]}"; do publish "${cVMAP[$K]}" "$(read_file "$K")"; done

  publish "main/serial"            "$(read_file /etc/serial)"
  publish "main/hw_model"          "$(grep 'hw_model='          /etc/libiio.ini | sed 's,hw_model=,,')"
  publish "main/fw_version"        "$(grep 'fw_version='        /etc/libiio.ini | sed 's,fw_version=,,')"
  publish "main/firmware_version"  "$(grep 'firmware_version='  /etc/libiio.ini | sed 's,firmware_version=,,')"
  publish "main/linux"             "$(uname -r)"
  
  local _uboot_ver; _uboot_ver=$(grep '^uboot '     /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
  [ -n "$_uboot_ver" ]   && publish "main/uboot"     "$_uboot_ver"
  local _br_ver;    _br_ver=$(grep '^buildroot ' /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
  [ -n "$_br_ver" ]      && publish "main/buildroot"  "$_br_ver"
  local _fpga_ver;  _fpga_ver=$(grep '^fpga '     /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
  [ -n "$_fpga_ver" ]    && publish "main/fpga"       "$_fpga_ver"
  [ -n "$IIO_VERSION" ] && publish "main/iio" "$IIO_VERSION"

  local _iface _cidr _prefix _mac _gw _dns
  publish "net/hostname" "$(hostname 2>/dev/null || echo 'n/a')"
  _gw=$(ip route show default 2>/dev/null | awk '/default/{print $3; exit}')
  publish "net/gateway" "${_gw:-n/a}"
  _dns=$(awk '/^nameserver/{print $2; exit}' /etc/resolv.conf 2>/dev/null)
  publish "net/dns" "${_dns:-n/a}"
  for _iface in usb0 eth0; do
    [ -d "/sys/class/net/$_iface" ] || continue
    _cidr=$(ip -4 addr show "$_iface" 2>/dev/null | awk '/inet /{print $2; exit}')
    _mac=$(ip link show "$_iface" 2>/dev/null | awk '/ether/{print $2; exit}')
    [ -n "$_cidr" ] && publish "net/$_iface/ip"   "${_cidr%%/*}"
    if [ -n "$_cidr" ] && [[ "$_cidr" == */* ]]; then
      publish "net/$_iface/mask" "$(prefix_to_mask "${_cidr##*/}")"
    fi
    [ -n "$_mac" ] && publish "net/$_iface/mac" "$_mac"
  done

  sweep=$(iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable 2>/dev/null)
  if [ "$sweep" == "1" ]; then
    publish "rx/sweep/activate" "1"
  else
    publish "rx/sweep/activate" "0"
  fi
  echo "$sweep" > /tmp/sweep_on

  sweep_frequency=$(cat /tmp/sweep_frequency 2>/dev/null)
  sweep_span=$(cat /tmp/sweep_span 2>/dev/null)
  publish "rx/sweep/frequency" "${sweep_frequency:-0}"
  publish "rx/span" "${sweep_span:-0}"
  publish "main/fir_config" "$(head -1 "${folder}filter_fir_config" 2>/dev/null || echo 'n/a')"

  publish "rx/buffer_size" "$(read_file "${adc_folder}buffer/length")"
  publish "tx/buffer_size" "$(read_file "${dds_folder}buffer/length")"

  

  rx_overload=$(iio_attr -D ad9361-phy direct_reg_access 0x5E 2>/dev/null)
  rx_overload=$((rx_overload & 1))
  publish "rx/overload" "$rx_overload"

  tx_overload=$(iio_attr -D ad9361-phy direct_reg_access 0x5F 2>/dev/null)
  if (( (tx_overload & 0x7C) != 0 )); then
    publish "tx/overload" "1"
  else
    publish "tx/overload" "0"
  fi

  read -r _ cpu_u cpu_n cpu_s cpu_id cpu_iw cpu_ir cpu_sf _ < /proc/stat
  local cpu_total=$(( cpu_u + cpu_n + cpu_s + cpu_id + cpu_iw + cpu_ir + cpu_sf ))
  local cpu_work=$(( cpu_total - cpu_id - cpu_iw ))
  if [ "$CPU_PREV_TOTAL" -gt 0 ]; then
    local cpu_dt=$(( cpu_total - CPU_PREV_TOTAL ))
    local cpu_dw=$(( cpu_work - CPU_PREV_WORK ))
    local cpu_sample=0
    [ "$cpu_dt" -gt 0 ] && cpu_sample=$(( cpu_dw * 100 * 2 / cpu_dt ))
    [ "$cpu_sample" -gt 100 ] && cpu_sample=100
    CPU_EMA=$(( (CPU_EMA * 7 + cpu_sample * 3) / 10 ))
    publish "main/cpu" "$CPU_EMA"
  fi
  CPU_PREV_TOTAL=$cpu_total
  CPU_PREV_WORK=$cpu_work

  local mem_total=1 mem_avail=1
  while IFS=' ' read -r mem_key mem_val _; do
    case $mem_key in
      MemTotal:)     [ "$mem_val" -gt 0 ] && mem_total=$mem_val ;;
      MemAvailable:) mem_avail=$mem_val; break ;;
    esac
  done < /proc/meminfo
  publish "main/mem" $(( (mem_total - mem_avail) * 100 / mem_total ))

  local temp_raw; temp_raw=$(read_file "${folder}in_temp0_input")
  [ "$temp_raw" != "n/a" ] && publish "main/temp" $(( temp_raw / 1000 ))

  local fpga_raw; fpga_raw=$(read_file "${xadc_folder}in_temp0_input")
  if [ "$fpga_raw" = "n/a" ] && [ -n "$xadc_folder" ] && [ "$xadc_folder" != "/" ]; then
    local xraw xoff; xraw=$(read_file "${xadc_folder}in_temp0_raw"); xoff=$(read_file "${xadc_folder}in_temp0_offset")
    [ "$xraw" != "n/a" ] && fpga_raw=$(( (xraw + xoff) * 123 ))
  fi
  [ "$fpga_raw" != "n/a" ] && publish "main/fpga_temp" $(( fpga_raw / 1000 - 20 ))

  local uptime_s; read -r uptime_s _ < /proc/uptime
  publish "main/uptime" "${uptime_s%%.*}"

  local tx_dma_now tx_dma_rate=0
  tx_dma_now=$(grep "7c42" /proc/interrupts 2>/dev/null | head -1 | awk '{s=0; for(i=2;i<=NF;i++){if($i~/^[0-9]+$/)s+=$i}; print s}')
  if [ -n "$tx_dma_now" ] && [ "$TX_DMA_PREV" -gt 0 ]; then
    tx_dma_rate=$(( (tx_dma_now - TX_DMA_PREV) / 2 ))
    publish "tx/dma_transfer" "$tx_dma_rate"
  fi
  [ -n "$tx_dma_now" ] && TX_DMA_PREV=$tx_dma_now

  tx_under_raw=$(iio_attr -D cf-ad9361-dds-core-lpc direct_reg_access 0x80000088 2>/dev/null)
  if [ -n "$tx_under_raw" ] && (( (tx_under_raw & 4) != 0 )) && (( tx_dma_rate > 0 )); then
    publish "tx/underflow" "1"
    iio_attr -D cf-ad9361-dds-core-lpc direct_reg_access "0x80000088 $tx_under_raw" >/dev/null 2>&1
  else
    publish "tx/underflow" "0"
  fi

  local rx_dma_now rx_dma_rate=0
  rx_dma_now=$(grep "7c40" /proc/interrupts 2>/dev/null | head -1 | awk '{s=0; for(i=2;i<=NF;i++){if($i~/^[0-9]+$/)s+=$i}; print s}')
  if [ -n "$rx_dma_now" ] && [ "$RX_DMA_PREV" -gt 0 ]; then
    rx_dma_rate=$(( (rx_dma_now - RX_DMA_PREV) / 2 ))
    publish "rx/dma_transfer" "$rx_dma_rate"
  fi
  [ -n "$rx_dma_now" ] && RX_DMA_PREV=$rx_dma_now

  rx_under_raw=$(iio_attr -D cf-ad9361-lpc direct_reg_access 0x80000088 2>/dev/null)
  if [ -n "$rx_under_raw" ] && (( (rx_under_raw & 4) != 0 )) && (( rx_dma_rate > 0 )); then
    publish "rx/underflow" "1"
    iio_attr -D cf-ad9361-lpc direct_reg_access "0x80000088 $rx_under_raw" >/dev/null 2>&1
  else
    publish "rx/underflow" "0"
  fi

  local iq_rx iq_tx
  read -r iq_rx iq_tx <<< "$(grep "${IQ_IFF}:" /proc/net/dev 2>/dev/null | awk '{print $2, $10}')"
  if [ -n "$iq_rx" ]; then
    if [ "$IQ_INIT" -eq 1 ]; then
      publish "rx/rate" $(( (iq_rx - IQ_RX_PREV) / 2 ))
      publish "tx/rate" $(( (iq_tx - IQ_TX_PREV) / 2 ))
    fi
    IQ_RX_PREV=$iq_rx
    IQ_TX_PREV=$iq_tx
    IQ_INIT=1
  fi

  local usb_rx usb_tx
  read -r usb_rx usb_tx <<< "$(grep "usb0:" /proc/net/dev 2>/dev/null | awk '{print $2, $10}')"
  if [ -n "$usb_rx" ]; then
    if [ "$USB_INIT" -eq 1 ]; then
      publish "usb/rx_rate" $(( (usb_rx - USB_RX_PREV) / 2 ))
      publish "usb/tx_rate" $(( (usb_tx - USB_TX_PREV) / 2 ))
    fi
    USB_RX_PREV=$usb_rx
    USB_TX_PREV=$usb_tx
    USB_INIT=1
  fi
}

update_sweep () {
  /root/sweep.sh "$1" "$2" "$3"
}

# ============================================================
#  COMMAND PARSER
# ============================================================

parse_cmd () {
  local cmd="${1//cmd\//}"
  local val="$2"

  if [ "${rVMAP[$cmd]}" ]; then
    echo "${val}" > "${rVMAP[$cmd]}"
    publish_force "$cmd" "$(read_file "${rVMAP[$cmd]}")"
  else
    case $cmd in
    rx/rfinput)          /root/switch_rfinput.sh "rx${val}" & ;;
    tx/rfinput)          /root/switch_rfoutput.sh "tx${val}" & ;;
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
      SPAN=$(cat /tmp/sweep_span 2>/dev/null)
      sweep_on=$(cat /tmp/sweep_on 2>/dev/null)
      echo "$val" > /tmp/sweep_frequency
      if [ "$sweep_on" == "1" ]; then
        update_sweep "$val" "${SPAN:-0}" 1
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
      sweep_on=$(cat /tmp/sweep_on 2>/dev/null)
      if [ "$val" == "1" ]; then
        sweep_on=$val
        update_sweep "$sweep_frequency" "$sweep_span" "$sweep_on"
      else
        /root/sweep_stop.sh
      fi
    ;;
    rx/loopback)
      echo "$val" > "${debug_folder}loopback" 2>/dev/null
      if [ "$val" = "2" ]; then
        killall -9 watchconsoletx.sh 2>/dev/null
        echo 0 > "${folder}out_altvoltage1_TX_LO_powerdown"
      else
        /root/watchconsoletx.sh &
      fi
      publish_force "rx/loopback" "$(read_file "${debug_folder}loopback")"
    ;;
    system/reboot)
      if [ "$val" = "poweroff" ]; then poweroff; else reboot; fi
    ;;
    system/logrequest)
      ( dmesg 2>/dev/null | while IFS= read -r line; do
          /usr/bin/mosquitto_pub -i "tezuka_log" -t "state/system/log" -m "$line"
        done ) &
    ;;
    system/getenv)
      if [[ "$val" == "all" ]]; then
        (
          n=0; cur_name=""; cur_val=""
          while IFS= read -r line; do
            if [[ "$line" == *=* ]] && [[ "${line%%=*}" =~ ^[a-zA-Z0-9_]+$ ]]; then
              if [[ -n "$cur_name" ]]; then
                /usr/bin/mosquitto_pub -r -i "tezuka_env" \
                  -t "state/system/env/$cur_name" -m "$cur_val"
                (( n++ ))
              fi
              cur_name="${line%%=*}"
              cur_val="${line#*=}"
            elif [[ -n "$cur_name" ]]; then
              cur_val+=$'\n'"$line"
            fi
          done < <(fw_printenv 2>/dev/null)
          if [[ -n "$cur_name" ]]; then
            /usr/bin/mosquitto_pub -r -i "tezuka_env" \
              -t "state/system/env/$cur_name" -m "$cur_val"
            (( n++ ))
          fi
          /usr/bin/mosquitto_pub -r -i "tezuka_env" -t "state/system/env_count" -m "$n"
        ) &
      else
        [[ "$val" =~ ^[a-zA-Z0-9_]+$ ]] || return
        ( result=$(fw_printenv "$val" 2>/dev/null)
          /usr/bin/mosquitto_pub -r -i "tezuka_env" \
            -t "state/system/env/$val" -m "${result#*=}" ) &
      fi
    ;;
    system/setenv/*)
      local param="${cmd#system/setenv/}"
      [[ "$param" =~ ^[a-zA-Z0-9_]+$ ]] || return
      ( fw_setenv "$param" "$val" 2>/dev/null
        result=$(fw_printenv "$param" 2>/dev/null)
        /usr/bin/mosquitto_pub -r -i "tezuka_env" \
          -t "state/system/env/$param" -m "${result#*=}" ) &
    ;;
    system/kalibrate/scan)
      (
        KID="tezuka_kal_$$"
        kpub()  { /usr/bin/mosquitto_pub    -i "$KID" -t "state/system/kalibrate/$1" -m "$2"; }
        kpub_r(){ /usr/bin/mosquitto_pub -r -i "$KID" -t "state/system/kalibrate/$1" -m "$2"; }
        kpub_r "status" "scanning"
        echo "$XO_BASE" > "${folder}xo_correction" 2>/dev/null
        cur_gain=$(awk '{print int($1)}' "${folder}in_voltage0_hardwaregain" 2>/dev/null)
        json="["; first=1
        while IFS= read -r line; do
          kpub "log" "$line"
          if [[ "$line" =~ chan:[[:space:]]*([0-9]+)[[:space:]]*\(([0-9.]+)MHz.*power:[[:space:]]*([-0-9.]+) ]]; then
            [ "$first" -eq 0 ] && json+=","
            json+="{\"chan\":${BASH_REMATCH[1]},\"freq\":${BASH_REMATCH[2]},\"power\":${BASH_REMATCH[3]}}"
            first=0
          fi
        done < <(kal -s "$val" ${cur_gain:+-g "$cur_gain"} 2>&1)
        if [ "$first" -eq 1 ]; then
          kpub_r "status" "error"
        else
          json+="]"
          kpub_r "channels" "$json"
          kpub_r "status" "done"
        fi
      ) &
    ;;
    system/kalibrate/run)
      (
        KID="tezuka_kal_$$"
        kpub()  { /usr/bin/mosquitto_pub    -i "$KID" -t "state/system/kalibrate/$1" -m "$2"; }
        kpub_r(){ /usr/bin/mosquitto_pub -r -i "$KID" -t "state/system/kalibrate/$1" -m "$2"; }
        kpub_r "status" "calibrating"
        echo "$XO_BASE" > "${folder}xo_correction" 2>/dev/null
        cur_gain=$(awk '{print int($1)}' "${folder}in_voltage0_hardwaregain" 2>/dev/null)
        ppm=""; ppb=""
        while IFS= read -r line; do
          kpub "log" "$line"
          if [[ "$line" =~ [Ee]rror:[[:space:]]*([-0-9.]+)[[:space:]]ppm[[:space:]]*\(([-0-9.]+)[[:space:]]ppb\) ]]; then
            ppm="${BASH_REMATCH[1]}"
            ppb="${BASH_REMATCH[2]}"
          fi
        done < <(kal -c "$val" ${cur_gain:+-g "$cur_gain"} 2>&1)
        if [ -n "$ppm" ]; then
          new_xo=$(awk "BEGIN{printf \"%d\", $XO_BASE + $XO_BASE * $ppm / 1000000}")
          echo "$new_xo" > "${folder}xo_correction" 2>/dev/null
          actual_xo=$(<"${folder}xo_correction" 2>/dev/null)
          /usr/bin/mosquitto_pub -r -i "$KID" -t "state/main/freq_correction" -m "${actual_xo:-$new_xo}"
          kpub_r "result_ppm" "$ppm"
          kpub_r "result_ppb" "$ppb"
          kpub_r "status" "done"
        else
          kpub_r "status" "error"
        fi
      ) &
    ;;
    esac
  fi
}

# ============================================================
#  STATE FOR DELTA CALCULATIONS
# ============================================================

CPU_PREV_TOTAL=0
CPU_PREV_WORK=0
CPU_EMA=0       
TX_DMA_PREV=0
RX_DMA_PREV=0
IQ_IFF="eth0"   
IQ_RX_PREV=0
IQ_TX_PREV=0
IQ_INIT=0       
USB_RX_PREV=0
USB_TX_PREV=0
USB_INIT=0

# ============================================================
#  MAIN LOOP
# ============================================================

# Subscribe to commands cleanly parsing topic and payloads with spaces
/usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "cmd/#" | while read -r incoming_topic incoming_payload; do 
  parse_cmd "$incoming_topic" "$incoming_payload"
done &

# Poll dynamic data every 2 seconds
while true; do
  dump_data
  sleep 2
done