#!/bin/bash
# Modified by Christos Nikolaou (SV1EIA) 2026.
# Christos Nikolaou can be reached by email at : sv1eia@gmail.com

SERIAL_PORT="/dev/ttyACM0"
MQTT_FIFO="/tmp/mqtt_fifo"
WG_CONF="/mnt/jffs2/etc/wireguard/wg0.conf"
CLASSIFIER_TEMPLATES="/mnt/jffs2/classifier/templates.bin"
BANDPLAN_FILE="/mnt/jffs2/bandplan.json"

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
STATIC_HW_MODEL=$(grep 'hw_model='                 /etc/libiio.ini | sed 's,hw_model=,,')
STATIC_FW_VERSION=$(grep 'fw_version='             /etc/libiio.ini | sed 's,fw_version=,,')
STATIC_FIRMWARE_VERSION=$(grep 'firmware_version=' /etc/libiio.ini | sed 's,firmware_version=,,')
STATIC_LINUX=$(uname -r)
STATIC_UBOOT=$(grep '^uboot '     /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
STATIC_BR=$(grep '^buildroot '    /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
STATIC_FPGA=$(grep '^fpga '       /opt/VERSIONS 2>/dev/null | cut -d' ' -f2)
HAVE_GPSPIPE=0; command -v gpspipe >/dev/null 2>&1 && HAVE_GPSPIPE=1

# debugfs folders of the ADC and DDS cores (their direct_reg_access holds the
# underflow/overflow status read in dump_data)
adc_debug_folder=""; [ "$adc_folder" != "/" ] && adc_debug_folder="/sys/kernel/debug/iio/$(basename "${adc_folder%/}")/"
dds_debug_folder=""; [ "$dds_folder" != "/" ] && dds_debug_folder="/sys/kernel/debug/iio/$(basename "${dds_folder%/}")/"

# Base XO oscillator frequency — captured before any correction is applied
XO_BASE=$(<"${folder}xo_correction" 2>/dev/null)
XO_BASE=${XO_BASE:-40000000}

# axi_vcxo_ctrl reference-discipline IP — present only on some boards
# (currently libre). Detected once from the live devicetree, base address
# taken from the node name itself (vcxoctrl@<hex base>), rather than
# assumed — boards without this node/status=okay just get VCXO_BASE="" and
# fall back to the old refclk_source-only reporting in dump_data().
VCXO_BASE=""
for _vn in /sys/firmware/devicetree/base/amba_pl@0/vcxoctrl@*; do
  [ -e "$_vn" ] || continue
  _vst=$(tr -d '\0' < "$_vn/status" 2>/dev/null)
  if [ "$_vst" = "okay" ] || [ "$_vst" = "ok" ]; then
    VCXO_BASE="0x${_vn##*@}"
  fi
  break
done
unset _vn _vst

# ADF4001 reference control (PlutoSky R2 and boards built the same way).
# The board overlay describes the EMIO lines in /etc/refclk.conf; without
# that file, or without the libgpiod tools, this block stays inert and
# dump_data() falls back to the refclk_source-only reporting.
REFCLK_HW=""
REFCLK_CHIP=""
if [ -r /etc/refclk.conf ]; then
  . /etc/refclk.conf
fi
if [ "$REFCLK_HW" = adf4001 ] && command -v gpioget >/dev/null 2>&1; then
  REFCLK_CHIP=$(gpiodetect 2>/dev/null | sed -n "s/^\(gpiochip[0-9]*\) \[${REFCLK_CHIP_LABEL:-zynq_gpio}\].*/\1/p" | head -n 1)
  [ -n "$REFCLK_CHIP" ] || REFCLK_HW=""
else
  REFCLK_HW=""
fi

# Reads every reference line in one gpioget call and sets REF_PRESENT REF_CP
# REF_LOCK REF_STATE REF_COUNT. Returns 1 when the lines cannot be read.
refclk_read () {
  local base=${REFCLK_EMIO_BASE:-54} offs i vals w b
  offs="$(( base + ${REFCLK_I_PRESENT:-33} )) $(( base + ${REFCLK_I_CP:-35} )) $(( base + ${REFCLK_I_LOCK:-52} )) $(( base + ${REFCLK_I_STATE:-53} )) $(( base + ${REFCLK_I_STATE:-53} + 1 ))"
  i=0
  while [ $i -lt 16 ]; do
    offs="$offs $(( base + ${REFCLK_I_COUNT:-36} + i ))"
    i=$((i + 1))
  done
  vals=$(gpioget "$REFCLK_CHIP" $offs 2>/dev/null) || return 1
  set -- $vals
  [ $# -eq 21 ] || return 1
  REF_PRESENT=$1; REF_CP=$2; REF_LOCK=$3
  case "$4$5" in
    00) REF_STATE=absent ;;
    10) REF_STATE=acquire ;;
    01) REF_STATE=locked ;;
    11) REF_STATE=recheck ;;
    *)  REF_STATE=unknown ;;
  esac
  shift 5
  REF_COUNT=0; w=1
  for b in "$@"; do
    REF_COUNT=$(( REF_COUNT + b * w )); w=$(( w * 2 ))
  done
  return 0
}

# auto | external | internal, from the u-boot environment (see S22refclk)
refclk_mode () {
  local a s
  a=$(fw_printenv -n refclk_auto 2>/dev/null | tr -cd '[a-zA-Z0-9]')
  s=$(fw_printenv -n refclk_source 2>/dev/null | tr -cd '[a-zA-Z0-9]')
  case "$a" in
    off|no|0) if [ "$s" = external ]; then echo external; else echo internal; fi ;;
    *) echo auto ;;
  esac
}

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
  [${debug_folder}adi,1rx-1tx-mode-use-tx-num]='tx/rfoutput'
  [${debug_folder}adi,2rx-2tx-mode-enable]='system/2r2t'
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

# mqtt_pubd keeps ONE persistent MQTT connection open for the life of this
# script and publishes each "topic\tpayload" line from the FIFO on it —
# no per-message process spawn, no per-message TCP/MQTT handshake. Replaces
# the previous "spawn mosquitto_pub per line" reader, which made a burst of
# commands (e.g. rapid frequency tuning, each triggering several state
# echoes) visibly lag/"buffer" behind the connect+publish+disconnect cost
# of each individual mosquitto_pub invocation.
/usr/bin/mqtt_pubd -i "tezuka_pub" < "$MQTT_FIFO" &
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


# Fork-free counterparts of read_file for the 2 s status loop. rd sets REPLY
# to what "$(read_file f)" would give (n/a when f is not readable); rd_raw to
# what "$(cat f 2>/dev/null)" would give (empty when missing). Both strip the
# trailing newlines as command substitution does.
rd_raw () {
  REPLY=""
  { IFS= read -r -d '' REPLY < "$1"; } 2>/dev/null
  while [[ $REPLY == *$'\n' ]]; do REPLY=${REPLY%$'\n'}; done
  return 0
}

rd () {
  if [ -r "$1" ]; then rd_raw "$1"; else REPLY='n/a'; fi
}

# A register through an IIO device's debugfs direct_reg_access, as
# "iio_attr -D <dev> direct_reg_access <addr>" does: write the address, read
# the value back. REPLY is empty when the device has no debugfs entry.
reg_read () {
  REPLY=""
  [ -n "$1" ] && [ -w "${1}direct_reg_access" ] || return 1
  echo "$2" > "${1}direct_reg_access" 2>/dev/null || return 1
  rd_raw "${1}direct_reg_access"
}

# The whole u-boot environment with one fw_printenv call, into FWENV
declare -A FWENV
load_fwenv () {
  local _l
  FWENV=()
  while IFS= read -r _l; do
    [[ $_l == *=* ]] && FWENV[${_l%%=*}]=${_l#*=}
  done < <(fw_printenv 2>/dev/null)
}

# prefix_to_mask without the subshell: the mask in REPLY
prefix_to_mask_v () {
  local p=$1 i octet m="" b
  for i in 1 2 3 4; do
    b=$(( p > 8 ? 8 : p < 0 ? 0 : p ))
    octet=$(( b == 0 ? 0 : 256 - (1 << (8 - b)) ))
    m="${m:+$m.}$octet"
    p=$(( p - b ))
  done
  REPLY=$m
}

# Boolean state ("1"/"0") for a /sys/class/leds/<name> — used by both the
# periodic gpio/<name> publish below and the cmd/gpio/<name> handler, so
# both always agree on what "on" means regardless of that LED's actual
# max_brightness range (some are 0/1 GPIO LEDs, some are 0-255 PWM).
led_state () {
  local b; b=$(read_file "/sys/class/leds/$1/brightness")
  if [ "$b" != "n/a" ] && [ "$b" -gt 0 ] 2>/dev/null; then echo 1; else echo 0; fi
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

# Publishes non-secret WireGuard tunnel status only. The private key (and any
# peer preshared-key) must NEVER reach a state/ topic — the dashboard's MQTT
# broker is unauthenticated (allow_anonymous, no TLS on the WS port it uses),
# and every state/ publish is retained, so anything sent here is effectively
# permanent and world-readable. "wg show <iface> dump"'s first line is the
# interface line whose first field is the actual PRIVATE key — it is
# intentionally never read here. The device's own public key comes from the
# separate "wg show <iface> public-key" call instead, and on the one peer
# line we keep, field 2 (preshared-key) is explicitly discarded.
publish_vpn_status () {
  local up=0 pubkey="" peer_pubkey="" endpoint="" allowed_ips="" handshake=0 rx=0 tx=0 _psk _keepalive
  if [ -e /sys/class/net/wg0 ]; then
    up=1
    pubkey=$(wg show wg0 public-key 2>/dev/null)
    read -r peer_pubkey _psk endpoint allowed_ips handshake rx tx _keepalive \
      < <(wg show wg0 dump 2>/dev/null | tail -n +2 | head -1)
  fi
  local json upb=false
  [ "$up" = 1 ] && upb=true
  printf -v json '{"up":%s,"pubkey":"%s","peer_pubkey":"%s","endpoint":"%s","allowed_ips":"%s","latest_handshake":%s,"rx_bytes":%s,"tx_bytes":%s}' \
    "$upb" \
    "$pubkey" "${peer_pubkey:-}" "${endpoint:-}" "${allowed_ips:-}" "${handshake:-0}" "${rx:-0}" "${tx:-0}"
  publish "vpn/status" "$json"
}

# ============================================================
#  DATA PUBLISHING
# ============================================================

dump_data () {
  # Everything below runs every 2 s, so it avoids forking: files are read
  # with rd (bash builtins, no $(...) subshell), the u-boot environment is
  # read with one fw_printenv per cycle, and the AD9361/DMA status registers
  # go through debugfs directly instead of iio_attr. The published topics and
  # values are the same as before.
  local K
  for K in "${!VMAP[@]}"; do rd "$K"; publish "${VMAP[$K]}" "$REPLY"; done
  for K in "${!cVMAP[@]}"; do rd "$K"; publish "${cVMAP[$K]}" "$REPLY"; done

  rd "${folder}xo_correction"; publish "system/xo_correction" "$REPLY"

  load_fwenv
  [ -n "${FWENV[overclock_profile]}" ] && publish "system/overclock" "${FWENV[overclock_profile]}"

  rd /etc/serial; publish "main/serial" "$REPLY"
  publish "main/hw_model"          "$STATIC_HW_MODEL"
  publish "main/fw_version"        "$STATIC_FW_VERSION"
  publish "main/firmware_version"  "$STATIC_FIRMWARE_VERSION"
  publish "main/linux"             "$STATIC_LINUX"

  [ -n "$STATIC_UBOOT" ] && publish "main/uboot"     "$STATIC_UBOOT"
  [ -n "$STATIC_BR" ]    && publish "main/buildroot" "$STATIC_BR"
  [ -n "$STATIC_FPGA" ]  && publish "main/fpga"      "$STATIC_FPGA"
  [ -n "$IIO_VERSION" ] && publish "main/iio" "$IIO_VERSION"

  # Station identity — device is the source of truth (u-boot env), the
  # Dashboard's Operator page reflects these instead of its own localStorage.
  publish "operator/name"     "${FWENV[operator_name]}"
  publish "operator/callsign" "${FWENV[call]}"
  publish "operator/locator"  "${FWENV[locator]}"

  if [ "${FWENV[wireguard_enabled]}" = "1" ]; then publish "vpn/enabled" on; else publish "vpn/enabled" off; fi
  publish_vpn_status

  if [ "${FWENV[classifier_enabled]}" = "1" ]; then publish "classifier/enabled" on; else publish "classifier/enabled" off; fi

  # Reference clock status. On boards with the vctcxo_lock IP (VCXO_BASE
  # set at startup — see top of file), read real registers per
  # vctcxo_lock.md: 0x0C dac_ref_sel selects the source (there is no
  # auto-classification bit in this IP's status word — 0x10 only carries
  # locked[0]/ref_present[1], bits [31:2] are unused), 0x08 live DAC
  # setpoint (the loop's actual correction output). "Frequency" here is
  # just the nominal rate implied by the selected mode, not a measured
  # value — this IP has no register for that (unlike the old
  # axi_vcxo_ctrl design this replaced on libre). Boards without the IP
  # fall back to the old refclk_source-only reporting (no real
  # lock-detect there).
  if [ -n "$VCXO_BASE" ]; then
    local _v0c _v10 _v08
    _v0c=$(devmem "$(( VCXO_BASE + 0x0C ))" 32 2>/dev/null)
    _v10=$(devmem "$(( VCXO_BASE + 0x10 ))" 32 2>/dev/null)
    _v08=$(devmem "$(( VCXO_BASE + 0x08 ))" 32 2>/dev/null)

    if [[ "$_v0c" =~ ^(0x)?[0-9a-fA-F]+$ ]]; then
      case $(( _v0c & 0x3 )) in
        0) publish "system/clkref/source" "10mhz"; publish "system/clkref/frequency" "10000000" ;;
        1|2) publish "system/clkref/source" "pps";  publish "system/clkref/frequency" "1" ;;
        *) publish "system/clkref/source" "none";   publish "system/clkref/frequency" "n/a" ;;
      esac
    else
      publish "system/clkref/source"    "n/a"
      publish "system/clkref/frequency" "n/a"
    fi

    if [[ "$_v10" =~ ^(0x)?[0-9a-fA-F]+$ ]]; then
      publish "system/clkref/lock" "$(( _v10 & 1 ))"
    else
      publish "system/clkref/lock" "0"
    fi

    if [[ "$_v08" =~ ^(0x)?[0-9a-fA-F]+$ ]]; then
      publish "system/clkref/correction" "$(( _v08 & 0xFFFF ))"
    else
      publish "system/clkref/correction" "n/a"
    fi
  elif [ "$REFCLK_HW" = adf4001 ] && refclk_read; then
    # ADF4001 boards: everything comes from the FPGA controller
    publish "system/clkref/hw"         "adf4001"
    publish "system/clkref/mode"       "$(refclk_mode)"
    publish "system/clkref/present"    "$REF_PRESENT"
    publish "system/clkref/lock"       "$REF_LOCK"
    publish "system/clkref/cp"         "$REF_CP"
    publish "system/clkref/state"      "$REF_STATE"
    publish "system/clkref/count"      "$REF_COUNT"
    if [ "$REF_PRESENT" = 1 ]; then
      publish "system/clkref/source"    "10mhz"
      publish "system/clkref/frequency" "${REFCLK_NOMINAL_HZ:-10000000}"
    else
      publish "system/clkref/source"    "none"
      publish "system/clkref/frequency" "n/a"
    fi
    publish "system/clkref/correction" "n/a"
  else
    local _clkref_src; _clkref_src=${FWENV[refclk_source]}
    _clkref_src=${_clkref_src:-internal}
    publish "system/clkref/source"      "$_clkref_src"
    if [ "$_clkref_src" = "external" ]; then
      publish "system/clkref/lock"      "0"
    else
      publish "system/clkref/lock"      "1"
    fi
    publish "system/clkref/frequency"   "n/a"
    publish "system/clkref/correction"  "n/a"
  fi

  local _iface _cidr _mac _gw _dns _line _f
  rd /proc/sys/kernel/hostname; publish "net/hostname" "${REPLY:-n/a}"
  # default route: the first line of /proc/net/route with destination 0,
  # gateway in little-endian hex
  _gw=""
  while read -r _iface _f _line _; do
    if [ "$_f" = "00000000" ] && [ "$_line" != "Gateway" ]; then
      _gw="$(( 0x${_line:6:2} )).$(( 0x${_line:4:2} )).$(( 0x${_line:2:2} )).$(( 0x${_line:0:2} ))"
      break
    fi
  done < /proc/net/route 2>/dev/null
  publish "net/gateway" "${_gw:-n/a}"
  _dns=""
  if [ -r /etc/resolv.conf ]; then
    while read -r _f _line _; do
      [ "$_f" = nameserver ] && { _dns=$_line; break; }
    done < /etc/resolv.conf
  fi
  publish "net/dns" "${_dns:-n/a}"
  # one ip call for the IPv4 addresses of all interfaces
  declare -A _addr4=()
  while read -r _ _iface _f _cidr _; do
    [ "$_f" = inet ] && [ -z "${_addr4[$_iface]}" ] && _addr4[$_iface]=$_cidr
  done < <(ip -o -4 addr show 2>/dev/null)
  for _iface in usb0 eth0; do
    [ -d "/sys/class/net/$_iface" ] || continue
    _cidr=${_addr4[$_iface]}
    rd "/sys/class/net/$_iface/address"; _mac=$REPLY
    [ "$_mac" = "n/a" ] && _mac=""
    [ -n "$_cidr" ] && publish "net/$_iface/ip"   "${_cidr%%/*}"
    if [ -n "$_cidr" ] && [[ "$_cidr" == */* ]]; then
      prefix_to_mask_v "${_cidr##*/}"; publish "net/$_iface/mask" "$REPLY"
    fi
    [ -n "$_mac" ] && publish "net/$_iface/mac" "$_mac"
  done

  # Handle Sweep Status Verification Safely
  # Re-derive sweep state from LIVE signals only (hardware fastlock pin +
  # current hardware SR) — never from the /tmp/sweep_on cache itself, which
  # otherwise self-latches: once a high SR (>43 MHz) sets it to "1", changing
  # the sample rate back down via cmd/rx/sampling (which doesn't touch this
  # file) left it stuck at "1" forever, permanently skipping the sync branch
  # below and leaving state/rx/span stuck at the old high value.
  local sweep_hw s_freq s_span hw_sr
  rd_raw "${debug_folder}adi,rx-fastlock-pincontrol-enable"; sweep_hw=$REPLY
  rd_raw /tmp/sweep_frequency; s_freq=$REPLY
  rd_raw /tmp/sweep_span; s_span=$REPLY
  rd "${folder}in_voltage_sampling_frequency"; hw_sr=$REPLY

  if [ "$sweep_hw" == "1" ] || { [ "$hw_sr" != "n/a" ] && [ "${hw_sr:-0}" -gt "43000000" ]; }; then
    publish "rx/sweep/engaged" "1"
    publish "rx/sweep/activate" "1"
    echo "1" > /tmp/sweep_on
  else
    publish "rx/sweep/engaged" "0"
    publish "rx/sweep/activate" "0"
    echo "0" > /tmp/sweep_on
    # In single-band mode (not sweep, not DDC), keep sweep_span in sync with the
    # actual hardware SR.  Fixes stale /tmp/sweep_span when api_controller starts
    # before the AD9361 settles at its final rate (e.g. init reads 2.4 MHz but
    # hardware later lands at 30.72 MHz).
    # Guard: skip when span < 2.4 MHz — that signals DDC mode where the display
    # span is intentionally smaller than the hardware SR.
    if [ "${s_span:-0}" -ge "2400000" ]; then
      if [ "$hw_sr" != "n/a" ] && [ -n "$hw_sr" ] && [ "${hw_sr:-0}" -ge "2400000" ]; then
        s_span=$hw_sr
        echo "$hw_sr" > /tmp/sweep_span
      fi
    fi
  fi

  publish "rx/sweep/frequency" "${s_freq:-0}"
  publish "rx/span"            "${s_span:-0}"
  publish "rx/sweep/span"      "${s_span:-0}"
  if [ -r "${folder}filter_fir_config" ]; then
    rd_raw "${folder}filter_fir_config"
    publish "main/fir_config" "${REPLY%%$'\n'*}"
  else
    publish "main/fir_config" "n/a"
  fi

  rd "${adc_folder}buffer/length"; publish "rx/buffer_size" "$REPLY"
  rd "${dds_folder}buffer/length"; publish "tx/buffer_size" "$REPLY"

  reg_read "$debug_folder" 0x5E; rx_overload=$REPLY
  rx_overload=$((rx_overload & 1))
  publish "rx/overload" "$rx_overload"

  reg_read "$debug_folder" 0x5F; tx_overload=$REPLY
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
    [ "$cpu_dt" -gt 0 ] && cpu_sample=$(( cpu_dw * 100 / cpu_dt ))
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

  local temp_raw; rd "${folder}in_temp0_input"; temp_raw=$REPLY
  [ "$temp_raw" != "n/a" ] && publish "main/temp" $(( temp_raw / 1000 ))

  local fpga_raw; rd "${xadc_folder}in_temp0_input"; fpga_raw=$REPLY
  if [ "$fpga_raw" = "n/a" ] && [ -n "$xadc_folder" ] && [ "$xadc_folder" != "/" ]; then
    local xraw xoff; rd "${xadc_folder}in_temp0_raw"; xraw=$REPLY; rd "${xadc_folder}in_temp0_offset"; xoff=$REPLY
    [ "$xraw" != "n/a" ] && fpga_raw=$(( (xraw + xoff) * 123 ))
  fi
  [ "$fpga_raw" != "n/a" ] && publish "main/fpga_temp" $(( fpga_raw / 1000 - 20 ))

  local uptime_s; read -r uptime_s _ < /proc/uptime
  publish "main/uptime" "${uptime_s%%.*}"

  # both DMA interrupt counts in one pass over /proc/interrupts (the sum of
  # every numeric field after the first, as the former awk did)
  local tx_dma_now="" rx_dma_now="" _sum _w
  while read -r _line; do
    case $_line in
      *7c42*|*7c40*)
        _sum=0
        set -f; set -- $_line; set +f; shift
        for _w in "$@"; do [[ $_w =~ ^[0-9]+$ ]] && _sum=$(( _sum + 10#$_w )); done
        case $_line in
          *7c42*) [ -z "$tx_dma_now" ] && tx_dma_now=$_sum ;;
          *)      [ -z "$rx_dma_now" ] && rx_dma_now=$_sum ;;
        esac ;;
    esac
  done < /proc/interrupts

  local tx_dma_rate=0
  if [ -n "$tx_dma_now" ] && [ "$TX_DMA_PREV" -gt 0 ]; then
    tx_dma_rate=$(( (tx_dma_now - TX_DMA_PREV) / 2 ))
    publish "tx/dma_transfer" "$tx_dma_rate"
  fi
  [ -n "$tx_dma_now" ] && TX_DMA_PREV=$tx_dma_now

  reg_read "$dds_debug_folder" 0x80000088; tx_under_raw=$REPLY
  if [ -n "$tx_under_raw" ] && (( (tx_under_raw & 4) != 0 )) && (( tx_dma_rate > 0 )); then
    publish "tx/underflow" "1"
    echo "0x80000088 $tx_under_raw" > "${dds_debug_folder}direct_reg_access" 2>/dev/null
  else
    publish "tx/underflow" "0"
  fi

  local rx_dma_rate=0
  if [ -n "$rx_dma_now" ] && [ "$RX_DMA_PREV" -gt 0 ]; then
    rx_dma_rate=$(( (rx_dma_now - RX_DMA_PREV) / 2 ))
    publish "rx/dma_transfer" "$rx_dma_rate"
  fi
  [ -n "$rx_dma_now" ] && RX_DMA_PREV=$rx_dma_now

  reg_read "$adc_debug_folder" 0x80000088; rx_under_raw=$REPLY
  if [ -n "$rx_under_raw" ] && (( (rx_under_raw & 4) != 0 )) && (( rx_dma_rate > 0 )); then
    publish "rx/underflow" "1"
    echo "0x80000088 $rx_under_raw" > "${adc_debug_folder}direct_reg_access" 2>/dev/null
  else
    publish "rx/underflow" "0"
  fi

  # both interfaces in one pass over /proc/net/dev (fields 2 and 10 of the
  # interface's line, as the former grep | awk did)
  local iq_rx="" iq_tx="" usb_rx="" usb_tx="" _a _b
  while read -r _line; do
    case $_line in
      "${IQ_IFF}:"*|usb0:*)
        set -f; set -- $_line; set +f
        _a=$2; _b=${10}
        case $_line in
          "${IQ_IFF}:"*) [ -z "$iq_rx" ] && { iq_rx=$_a; iq_tx=$_b; } ;;
        esac
        case $_line in
          usb0:*) [ -z "$usb_rx" ] && { usb_rx=$_a; usb_tx=$_b; } ;;
        esac ;;
    esac
  done < /proc/net/dev
  if [ -n "$iq_rx" ]; then
    if [ "$IQ_INIT" -eq 1 ]; then
      publish "rx/rate" $(( (iq_rx - IQ_RX_PREV) / 2 ))
      publish "tx/rate" $(( (iq_tx - IQ_TX_PREV) / 2 ))
    fi
    IQ_RX_PREV=$iq_rx
    IQ_TX_PREV=$iq_tx
    IQ_INIT=1
  fi

  if [ -n "$usb_rx" ]; then
    if [ "$USB_INIT" -eq 1 ]; then
      publish "usb/rx_rate" $(( (usb_rx - USB_RX_PREV) / 2 ))
      publish "usb/tx_rate" $(( (usb_tx - USB_TX_PREV) / 2 ))
    fi
    USB_RX_PREV=$usb_rx
    USB_TX_PREV=$usb_tx
    USB_INIT=1
  fi

  # iio_ws_proxy and gpsd: one pass over /proc/*/comm instead of pgrep/pidof
  local _have_proxy=0 _have_gpsd=0 _comm
  for _f in /proc/[0-9]*/comm; do
    read -r _comm < "$_f" 2>/dev/null || continue
    case $_comm in
      *iio_ws_proxy*) _have_proxy=1 ;;
    esac
    [ "$_comm" = gpsd ] && _have_gpsd=1
  done
  if [ "$_have_proxy" = 1 ]; then
    publish "system/iqtape" "on"
    publish "system/siggen" "on"
  else
    publish "system/iqtape" "off"
    publish "system/siggen" "off"
  fi

  # GPS fix and Maidenhead locator — poll via gpsd every 5 cycles (~10 s)
  if [ "$_have_gpsd" = 1 ] && [ "$HAVE_GPSPIPE" = 1 ]; then
    GPS_TICK=$(( GPS_TICK + 1 ))
    [ $(( GPS_TICK % 5 )) -eq 1 ] && poll_gps &
  else
    printf 'none' > /tmp/gps_fix
  fi
  local _gfix _gloc
  rd_raw /tmp/gps_fix; _gfix=$REPLY
  rd_raw /tmp/gps_locator; _gloc=$REPLY
  publish "system/gps/fix" "${_gfix:-none}"
  [ -n "$_gloc" ] && publish "system/gps/locator" "$_gloc"

  # Board LEDs (leds-gpio, /sys/class/leds/<label>) — reported to the
  # Dashboard's GPIO page as gpio/<label>. Note led0 normally carries the
  # kernel "heartbeat" trigger, so it will keep blinking on its own in the
  # UI; that trigger also means a manual cmd/gpio/led0 toggle can get
  # immediately overridden by the next heartbeat pulse.
  local _led_dir _led _b
  for _led_dir in /sys/class/leds/*/; do
    [ -d "$_led_dir" ] || continue
    _led=${_led_dir%/}; _led=${_led##*/}
    rd "${_led_dir}brightness"; _b=$REPLY
    if [ "$_b" != "n/a" ] && [ "$_b" -gt 0 ] 2>/dev/null; then publish "gpio/$_led" 1; else publish "gpio/$_led" 0; fi
  done
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
#  GPS HELPER
# ============================================================

poll_gps () {
  local raw fix="none" lat="" lon="" locator=""
  raw=$(timeout 3 gpspipe -w -n 15 2>/dev/null | grep -m1 '"class":"TPV"')
  if [ -n "$raw" ]; then
    local mode
    mode=$(printf '%s' "$raw" | awk -F'"mode":' 'NF>1{split($2,a,/[^0-9]/);print a[1]}')
    lat=$(printf '%s'  "$raw" | awk -F'"lat":'  'NF>1{split($2,a,/[,}]/);print a[1]}')
    lon=$(printf '%s'  "$raw" | awk -F'"lon":'  'NF>1{split($2,a,/[,}]/);print a[1]}')
    case "${mode:-0}" in
      3) fix="3D" ;;
      2) fix="2D" ;;
      *) fix="none" ;;
    esac
    if [ -n "$lat" ] && [ -n "$lon" ]; then
      locator=$(awk -v la="$lat" -v lo="$lon" 'BEGIN {
        lo += 180; la += 90
        A = int(lo / 20);        B = int(la / 10)
        C = int((lo % 20) / 2);  D = int(la % 10)
        E = int((lo % 2) * 12);  F = int((la % 1) * 24)
        printf "%c%c%d%d%c%c", 65+A, 65+B, C, D, 97+E, 97+F
      }')
      printf '%s' "$locator" > /tmp/gps_locator
    fi
  fi
  printf '%s' "$fix" > /tmp/gps_fix
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
    rx/rfinput)          /root/switch_rfinput.sh "rx${val}"; publish_force "rx/rfinput" "$(read_file "${debug_folder}adi,1rx-1tx-mode-use-rx-num")" ;;
    tx/rfoutput)         /root/switch_rfoutput.sh "tx${val}"; publish_force "tx/rfoutput" "$(read_file "${debug_folder}adi,1rx-1tx-mode-use-tx-num")" ;;
    
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
        if pgrep -x iio_ws_proxy >/dev/null 2>&1; then
          killall iio_ws_proxy 2>/dev/null
          while pgrep -x iio_ws_proxy >/dev/null 2>&1; do sleep 0.1; done
        fi
        /usr/bin/iio_ws_proxy -1 &
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
        if pgrep -x iio_ws_proxy >/dev/null 2>&1; then
          killall iio_ws_proxy 2>/dev/null
          while pgrep -x iio_ws_proxy >/dev/null 2>&1; do sleep 0.1; done
        fi
        /usr/bin/iio_ws_proxy -l -T &
        publish_force "system/siggen" "on"
        publish_force "system/iqtape" "on"
      else
        killall iio_ws_proxy 2>/dev/null
        publish_force "system/siggen" "off"
        publish_force "system/iqtape" "off"
      fi
    ;;
    system/clkref/source)
      # dac_ref_sel (vctcxo_lock reg 0x0C, bits[1:0]) is software-selected —
      # the IP has no auto-classification of what's on the wire (see
      # vctcxo_lock.md). Only meaningful on boards with the IP (VCXO_BASE set).
      [ -n "$VCXO_BASE" ] || return
      local _sel
      case "$val" in
        10mhz) _sel=0 ;;
        pps)   _sel=1 ;;
        none)  _sel=3 ;;
        *) return ;;
      esac
      local _cur; _cur=$(devmem "$(( VCXO_BASE + 0x0C ))" 32 2>/dev/null)
      [[ "$_cur" =~ ^(0x)?[0-9a-fA-F]+$ ]] || _cur=0
      devmem "$(( VCXO_BASE + 0x0C ))" 32 "$(( (_cur & ~0x3) | _sel ))" >/dev/null 2>&1
      publish_force "system/clkref/source" "$val"
    ;;
    system/clkref/mode)
      # ADF4001 boards: auto | external | internal, stored in the u-boot
      # environment and applied by S22refclk (see /etc/refclk.conf)
      [ "$REFCLK_HW" = adf4001 ] || return
      case "$val" in
        auto|external|internal) ;;
        *) return ;;
      esac
      /etc/init.d/S22refclk set "$val" >/dev/null 2>&1
      publish_force "system/clkref/mode" "$val"
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
    vpn/enabled)
      if [ "$val" = "on" ]; then
        if [ ! -f "$WG_CONF" ]; then
          publish_force "vpn/enabled" "off"
          return
        fi
        fw_setenv wireguard_enabled "1" 2>/dev/null &
        /etc/init.d/S51wireguard start
      else
        fw_setenv wireguard_enabled "0" 2>/dev/null &
        /etc/init.d/S51wireguard stop
      fi
      publish_force "vpn/enabled" "$val"
      publish_vpn_status &
    ;;
    vpn/config)
      # Payload is base64 (never raw conf text) — the mosquitto_sub/read loop
      # that feeds parse_cmd is line-based, so a multi-line wg-quick conf
      # would otherwise be shredded. This topic is a one-shot write: the
      # decoded conf (private key included) is never echoed back to any
      # state/ topic — see publish_vpn_status's comment on why.
      [[ "$val" =~ ^[A-Za-z0-9+/=]+$ ]] || return
      local tmp="/tmp/wg0.conf.$$"
      echo "$val" | base64 -d > "$tmp" 2>/dev/null || { rm -f "$tmp"; return; }
      # Sanitize artifacts commonly produced by third-party config
      # exporters before handing this to wg-quick: CRLF line endings, and
      # a trailing comma left on a comma-separated value (e.g.
      # "AllowedIPs = 0.0.0.0/0," with nothing after it) — the latter
      # makes wg setconf's address parser choke on an empty token.
      sed -i -e $'s/\r$//' -e 's/[[:space:]]*,[[:space:]]*$//' "$tmp"
      grep -q '^\[Interface\]' "$tmp" || { rm -f "$tmp"; return; }
      install -D -m 600 -o root -g root "$tmp" "$WG_CONF"
      rm -f "$tmp"
      if [ "$(fw_printenv -n wireguard_enabled 2>/dev/null)" = "1" ]; then
        /etc/init.d/S51wireguard restart
      fi
      publish_vpn_status &
    ;;
    classifier/enabled)
      if [ "$val" = "on" ]; then
        fw_setenv classifier_enabled "1" 2>/dev/null &
        /etc/init.d/S61classifier start
      else
        fw_setenv classifier_enabled "0" 2>/dev/null &
        /etc/init.d/S61classifier stop
      fi
      publish_force "classifier/enabled" "$val"
    ;;
    classifier/templates)
      # Same one-shot base64-over-MQTT transport as vpn/config, for the
      # same reason: the mosquitto_sub/read dispatch loop is line-based
      # and templates.bin is binary. Not secret like a private key, so
      # (unlike vpn/config) this is fine to be readable at 644.
      [[ "$val" =~ ^[A-Za-z0-9+/=]+$ ]] || return
      local tmp="/tmp/templates.bin.$$"
      echo "$val" | base64 -d > "$tmp" 2>/dev/null || { rm -f "$tmp"; return; }
      # Sanity-check the header (magic "SCFT" + version 1) before
      # installing — classifier.c would just log and idle on a bad file,
      # but rejecting garbage here avoids overwriting a working template
      # set with a corrupt upload.
      [ "$(head -c4 "$tmp")" = "SCFT" ] || { rm -f "$tmp"; return; }
      install -D -m 644 -o root -g root "$tmp" "$CLASSIFIER_TEMPLATES"
      rm -f "$tmp"
      if [ "$(fw_printenv -n classifier_enabled 2>/dev/null)" = "1" ]; then
        /etc/init.d/S61classifier restart
      fi
    ;;
    bandplan/data)
      # Plain JSON text, not base64 — unlike vpn/config and
      # classifier/templates (binary/sensitive), a band-plan edit is
      # JSON.stringify() output with no embedded newlines, so it's safe
      # as a normal one-shot MQTT command payload. Only a light
      # structural check here (real JSON validation would need a parser
      # this rootfs doesn't carry) — malformed input just fails to load
      # client-side on the next fetch, it can't corrupt anything else.
      # No daemon restart needed either: S59bandplan's symlink means
      # maia-httpd reads this file straight off disk on every request.
      case "$val" in
        \[*\]) ;;
        *) return ;;
      esac
      printf '%s' "$val" > "$BANDPLAN_FILE"
    ;;
    operator/name)
      [[ "$val" =~ ^[a-zA-Z0-9\ ._-]*$ ]] || return
      fw_setenv operator_name "$val" 2>/dev/null &
      publish_force "operator/name" "$val"
    ;;
    operator/callsign)
      # No "/" — it's a topic-level separator and this gets embedded in
      # pluto/<callsign>/... topics.
      [[ "$val" =~ ^[a-zA-Z0-9_-]*$ ]] || return
      fw_setenv call "$val" 2>/dev/null &
      publish_force "operator/callsign" "$val"
    ;;
    operator/locator)
      [[ "$val" =~ ^[a-zA-Z0-9]*$ ]] || return
      fw_setenv locator "$val" 2>/dev/null &
      publish_force "operator/locator" "$val"
    ;;
    gpio/*)
      local led="${cmd#gpio/}"
      [[ "$led" =~ ^[A-Za-z0-9_:.-]+$ ]] || return
      [ -d "/sys/class/leds/$led" ] || return
      if [ "$val" = "1" ]; then
        echo "$(read_file "/sys/class/leds/$led/max_brightness")" > "/sys/class/leds/$led/brightness" 2>/dev/null
      else
        echo 0 > "/sys/class/leds/$led/brightness" 2>/dev/null
      fi
      publish_force "gpio/$led" "$(led_state "$led")"
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
GPS_TICK=0

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
