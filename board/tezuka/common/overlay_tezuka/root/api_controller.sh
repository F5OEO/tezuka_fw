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

_init_freq=$(<"${folder}out_altvoltage0_RX_LO_frequency" 2>/dev/null)
_init_sr=$(<"${folder}in_voltage_sampling_frequency" 2>/dev/null)
echo "${_init_freq:-280000000}" > /tmp/sweep_frequency
echo "${_init_sr:-2400000}"    > /tmp/sweep_span
echo 0                          > /tmp/sweep_on

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

  publish "system/xo_correction" "$(read_file "${folder}xo_correction")"

  local _oc; _oc=$(fw_printenv overclock_profile 2>/dev/null | sed 's/overclock_profile=//')
  [ -n "$_oc" ] && publish "system/overclock" "$_oc"

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

  # Handle Sweep Status Verification Safely
  local sweep_hw; sweep_hw=$(cat "${debug_folder}adi,rx-fastlock-pincontrol-enable" 2>/dev/null)
  local sweep_sw; sweep_sw=$(cat /tmp/sweep_on 2>/dev/null)
  local s_freq; s_freq=$(cat /tmp/sweep_frequency 2>/dev/null)
  local s_span; s_span=$(cat /tmp/sweep_span 2>/dev/null)

  if [ "$sweep_hw" == "1" ] || [ "$sweep_sw" == "1" ] || [ "${s_span:-0}" -gt "43000000" ]; then
    publish "rx/sweep/engaged" "1"
    publish "rx/sweep/activate" "1"
    echo "1" > /tmp/sweep_on
  else
    publish "rx/sweep/engaged" "0"
    publish "rx/sweep/activate" "0"
    echo "0" > /tmp/sweep_on
  fi

  publish "rx/sweep/frequency" "${s_freq:-0}"
  publish "rx/span"            "${s_span:-0}"
  publish "rx/sweep/span"      "${s_span:-0}"
  publish "main/fir_config"    "$(head -1 "${folder}filter_fir_config" 2>/dev/null || echo 'n/a')"

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

  if pgrep iio_ws_proxy >/dev/null 2>&1; then
    publish "system/iqtape" "on"
    publish "system/siggen" "on"
  else
    publish "system/iqtape" "off"
    publish "system/siggen" "off"
  fi
}

do_sweep_stop () {
  echo 0 > "${debug_folder}adi,rx-fastlock-pincontrol-enable"
  cat "${folder}out_altvoltage0_RX_LO_frequency" > /dev/null
  echo 0 > "${folder}out_altvoltage0_RX_LO_fastlock_store"
  echo 0 > "${folder}out_altvoltage0_RX_LO_fastlock_recall"
}

do_sweep_start () {
  local FREQ_CENTRAL="$1" SPAN="$2"
  local FREQ_MINI=47000000 SR_MINI=2100000
  # SR wider than SPAN/8: only the inner 70% of each band is used, so step = SR*0.7
  # 8 * SR * 0.7 = SPAN  →  SR = SPAN * 5 / 28
  local SR=$(( SPAN * 5 / 28 ))
  [ "$SR" -lt "$SR_MINI" ] && SR=$SR_MINI
  # Centre-to-centre spacing between adjacent sub-bands
  local FREQ_STEP=$(( SR * 7 / 10 ))

  echo "manual" > "${folder}in_voltage0_gain_control_mode"
  publish_force "rx/gain_mode" "manual"
  echo 0 > "${folder}in_out_voltage_filter_fir_en"
  echo "$SR" > "${folder}in_voltage_sampling_frequency"
  echo $(( SR * 3 / 2 )) > "${folder}in_voltage_rf_bandwidth"

  local FREQ1=$(( FREQ_CENTRAL - 7 * FREQ_STEP / 2 ))
  if [ "$FREQ1" -lt "$FREQ_MINI" ]; then
    FREQ1=$FREQ_MINI
    FREQ_CENTRAL=$(( FREQ1 + 7 * FREQ_STEP / 2 ))
  fi

  local i FREQ
  for i in 0 1 2 3 4 5 6 7; do
    FREQ=$(( FREQ1 + i * FREQ_STEP ))
    echo "$FREQ" > "${folder}out_altvoltage0_RX_LO_frequency"
    echo "$i" > "${folder}out_altvoltage0_RX_LO_fastlock_store"
  done

  echo "$FREQ_CENTRAL" > "${folder}out_altvoltage0_RX_LO_frequency"
  echo 1 > "${debug_folder}adi,rx-fastlock-pincontrol-enable"
  echo 0 > "${folder}out_altvoltage0_RX_LO_fastlock_recall"

gpioset gpiochip0 63=0
gpioset gpiochip0 64=0
gpioset gpiochip0 65=0
  
}

# ============================================================
#  MAIA-HTTPD REST HELPERS
# ============================================================

spectro_fps () {
  (
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH http://localhost/api/spectrometer \
      -H "Content-Type: application/json" \
      -d "{\"output_sampling_frequency\": $1}" 2>/dev/null)
    if [ "${http_code:-0}" -ge 200 ] && [ "${http_code:-0}" -lt 300 ]; then
      publish_force "spectro/fps" "$1"
    else
      publish_force "spectro/fps" "error:${http_code:-unreachable}"
    fi
  ) &
}

spectro_mode () {
  (
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH http://localhost/api/spectrometer \
      -H "Content-Type: application/json" \
      -d "{\"mode\": \"$1\"}" 2>/dev/null)
    if [ "${http_code:-0}" -ge 200 ] && [ "${http_code:-0}" -lt 300 ]; then
      publish_force "spectro/mode" "$1"
    else
      publish_force "spectro/mode" "error:${http_code:-unreachable}"
    fi
  ) &
}

ddc_design () {
  (
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT http://localhost/api/ddc/design \
      -H "Content-Type: application/json" \
      -d "{\"passband_ripple\": 0.01, \"stopband_attenuation_db\": $2, \"decimation\": $1, \"stopband_one_over_f\": true, \"transition_bandwidth\": 0.05, \"frequency\": 0}" 2>/dev/null)
    if [ "${http_code:-0}" -ge 200 ] && [ "${http_code:-0}" -lt 300 ]; then
      publish_force "ddc/design" "$1/$2"
    else
      publish_force "ddc/design" "error:${http_code:-unreachable}"
    fi
  ) &
}

spectro_input () {
  (
    local http_code
    http_code=$(curl -s -o /dev/null -w "%{http_code}" -X PATCH http://localhost/api/spectrometer \
      -H "Content-Type: application/json" \
      -d "{\"input\": \"$1\"}" 2>/dev/null)
    if [ "${http_code:-0}" -ge 200 ] && [ "${http_code:-0}" -lt 300 ]; then
      publish_force "spectro/input" "$1"
    else
      publish_force "spectro/input" "error:${http_code:-unreachable}"
    fi
  ) &
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
    [ "$cmd" = "main/freq_correction" ] && publish_force "system/xo_correction" "$(read_file "${folder}xo_correction")"
  else
    case $cmd in
    rx/rfinput)          /root/switch_rfinput.sh "rx${val}" & ;;
    tx/rfinput)          /root/switch_rfoutput.sh "tx${val}" & ;;
    
    rx/span | rx/sweep/span)
      local SPAN; SPAN=$(printf "%0.f" "$val")
      echo "$SPAN" > /tmp/sweep_span

      local current_frequency; current_frequency=$(cat /tmp/sweep_frequency 2>/dev/null)
      current_frequency=${current_frequency:-280000000}

      # Clamp to max sweep span (SR = SPAN*5/28 must not exceed AD9361 max ~61 MHz)
      local SPAN_MAX=344000000
      [ "$SPAN" -gt "$SPAN_MAX" ] && SPAN=$SPAN_MAX
      echo "$SPAN" > /tmp/sweep_span

      # Threshold: 43 MHz = 61 MHz × 0.70 — max usable single-band span with 15% edge trim
      if [ "$SPAN" -gt "43000000" ]; then
        spectro_input "AD9361"
        echo "1" > /tmp/sweep_on
        publish_force "rx/sweep/activate"  "1"
        publish_force "rx/sweep/engaged"   "1"
        do_sweep_start "$current_frequency" "$SPAN"
      else
        echo "0" > /tmp/sweep_on
        publish_force "rx/sweep/activate"  "0"
        publish_force "rx/sweep/engaged"   "0"
        do_sweep_stop
        if [ "$SPAN" -lt 2400000 ]; then
          local DEC=$(( (2400000 ) / SPAN ))
          [ "$DEC" -lt 4 ] && DEC=4
          local SR=$(( DEC * SPAN ))
          echo "$SR" > "${folder}in_voltage_sampling_frequency" 2>/dev/null
          echo "$SPAN" > "${folder}in_voltage_rf_bandwidth" 2>/dev/null
          publish_force "rx/sampling"   "$(read_file "${folder}in_voltage_sampling_frequency")"
          publish_force "rx/bandwidth"  "$(read_file "${folder}in_voltage_rf_bandwidth")"
          if [ "$DEC" -lt 20 ]; then
            ddc_design "$DEC" 65
          else
            ddc_design "$DEC" 45
          fi
          spectro_input "DDC"
        else
          spectro_input "AD9361"  
          echo "$SPAN" > "${folder}in_voltage_sampling_frequency" 2>/dev/null
          echo "$SPAN" > "${folder}in_voltage_rf_bandwidth" 2>/dev/null
          publish_force "rx/sampling"   "$(read_file "${folder}in_voltage_sampling_frequency")"
          publish_force "rx/bandwidth"  "$(read_file "${folder}in_voltage_rf_bandwidth")"
        fi
      fi
      publish_force "rx/span"       "$SPAN"
      publish_force "rx/sweep/span" "$SPAN"
      local cur_fps; cur_fps=$(cat /tmp/spectro_fps 2>/dev/null)
      if [ -n "$cur_fps" ]; then
        if [ "$(cat /tmp/sweep_on 2>/dev/null)" = "1" ]; then
          spectro_fps $(( cur_fps * 8 ))
        else
          spectro_fps "$cur_fps"
        fi
      fi
    ;;

    rx/frequency | rx/sweep/frequency)
      local FREQ; FREQ=$(printf "%0.f" "$val")
      echo "$FREQ" > /tmp/sweep_frequency
      
      local current_span; current_span=$(cat /tmp/sweep_span 2>/dev/null)
      current_span=${current_span:-2400000}
      local sweep_on; sweep_on=$(cat /tmp/sweep_on 2>/dev/null)
      sweep_on=${sweep_on:-0}

      if [ "$sweep_on" == "1" ] || [ "$current_span" -gt "43000000" ]; then
        echo "1" > /tmp/sweep_on
        publish_force "rx/sweep/engaged" "1"
        do_sweep_start "$FREQ" "$current_span"
      else
        echo "0" > /tmp/sweep_on
        publish_force "rx/sweep/engaged" "0"
        echo "$FREQ" > "${folder}out_altvoltage0_RX_LO_frequency"
      fi
      publish_force "rx/frequency" "$FREQ"
      publish_force "rx/sweep/frequency" "$FREQ"
    ;;

    rx/sweep/activate)
      local current_frequency; current_frequency=$(cat /tmp/sweep_frequency 2>/dev/null)
      current_frequency=${current_frequency:-280000000}
      local current_span; current_span=$(cat /tmp/sweep_span 2>/dev/null)
      current_span=${current_span:-2400000}

      if [ "$val" == "1" ]; then
        echo "1" > /tmp/sweep_on
        publish_force "rx/sweep/engaged" "1"
        do_sweep_start "$current_frequency" "$current_span"
      else
        echo "0" > /tmp/sweep_on
        publish_force "rx/sweep/engaged" "0"
        do_sweep_stop
      fi
      publish_force "rx/sweep/activate" "$val"
    ;;

    spectro/fps)
      echo "$val" > /tmp/spectro_fps
      spectro_fps "$val"
      publish_force "spectro/fps" "$val"
    ;;

    spectro/mode)
      spectro_mode "$val"
      publish_force "spectro/mode" "$val"
    ;;

    spectro/input)
      spectro_input "$val"
    ;;

    ddc/design)
      ddc_design $(echo "$val" | cut -d'/' -f1) $(echo "$val" | cut -d'/' -f2)
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
    system/iqtape)
      if [ "$val" = "on" ]; then
        if ! pgrep -x iio_ws_proxy >/dev/null 2>&1; then
          /usr/bin/iio_ws_proxy &
        fi
        publish_force "system/iqtape" "on"
        publish_force "system/siggen" "on"
      else
        killall iio_ws_proxy 2>/dev/null
        publish_force "system/iqtape" "off"
        publish_force "system/siggen" "off"
      fi
    ;;
    system/siggen)
      if [ "$val" = "on" ]; then
        if ! pgrep -x iio_ws_proxy >/dev/null 2>&1; then
          /usr/bin/iio_ws_proxy &
        fi
        publish_force "system/siggen" "on"
        publish_force "system/iqtape" "on"
      else
        killall iio_ws_proxy 2>/dev/null
        publish_force "system/siggen" "off"
        publish_force "system/iqtape" "off"
      fi
    ;;
    system/reboot)
      if [ "$val" = "poweroff" ]; then poweroff; else reboot; fi
    ;;
    system/logrequest)
      ( dmesg 2>/dev/null | while IFS= read -r line; do
          /usr/bin/mosquitto_pub -i "tezuka_log" -t "state/system/log" -m "$line"
        done ) &
    ;;
    system/overclock_cap)
      (
        profiles=()
        while IFS= read -r f; do
          profiles+=("\"$(basename "$f")\"")
        done < <(find /boot/overclock -maxdepth 1 -type f 2>/dev/null | sort)
        json="[$(IFS=,; echo "${profiles[*]}")]"
        /usr/bin/mosquitto_pub -r -i "tezuka_oc" -t "state/system/overclock_cap" -m "$json"
      ) &
    ;;
    system/overclock)
      [[ "$val" =~ ^[a-zA-Z0-9_.+-]+$ ]] || return
      ( fw_setenv overclock_profile "$val" 2>/dev/null
        /usr/bin/mosquitto_pub -r -i "tezuka_oc" -t "state/system/overclock" -m "$val" ) &
    ;;
    system/getdebugiio)
      (
        while IFS= read -r filepath; do
          fname=$(basename "$filepath")
          fval=$(timeout 1 cat "$filepath" 2>/dev/null)
          /usr/bin/mosquitto_pub -i "tezuka_dbg" \
            -t "state/system/debugiio/$fname" -m "$fval"
        done < <(find "${debug_folder}" -maxdepth 1 -type f 2>/dev/null | sort)
      ) &
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
          /usr/bin/mosquitto_pub -r -i "$KID" -t "state/system/xo_correction" -m "${actual_xo:-$new_xo}"
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

# Subscribe to commands — reconnects automatically on broker disconnect
(
  while true; do
    /usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "cmd/#" 2>/dev/null \
      | while read -r incoming_topic incoming_payload; do
          parse_cmd "$incoming_topic" "$incoming_payload"
        done
    sleep 2
  done
) &

# Poll dynamic data every 2 seconds
while true; do
  dump_data
  sleep 2
done
