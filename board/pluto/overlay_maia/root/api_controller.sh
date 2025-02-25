#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"

# declare folder, files and their corresponding aliases (seen by client)

folder='/sys/bus/iio/devices/iio:device0/'

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

# based on VMAP, declare rVMAP
declare -A rVMAP
for K in "${!VMAP[@]}"; do rVMAP[${VMAP[$K]}]=$K; done

# Publish to mqtt
mqtt_publish () {
  /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "state/${1}" -m "${2}"
}

# Publish to serial port.
serial_publish () {
  if [ -e "${SERIAL_PORT}" ]; then
		echo "${1}" "${2}" > "${SERIAL_PORT}"
  fi
  echo "${1}" "${2}"
}

# Generic publish function
publish () {
  mqtt_publish "${1}" "${2}"
  serial_publish "${1}" "${2}"
}

# File reader with error handling
read_file () {
  if [ -r "$1" ]; then
    cat "${1}"
  else
    echo 'n/a'
  fi
}

# Publish data
update_data () {
  while read i; do
    k=${VMAP[$i]};
    v="`read_file $i`";
    publish ${k} ${v};
  done
}

# Collect all settings we want to report
dump_data () {
  for K in "${!VMAP[@]}"; do publish ${VMAP[$K]} "`read_file $K`"; done
  for K in "${!cVMAP[@]}"; do publish ${cVMAP[$K]} "`read_file $K`"; done
  publish "main/serial" "`read_file /etc/serial`"
  publish "main/hw_model" "`grep 'hw_model=' /etc/libiio.ini | sed s,hw_model=,,`"
  publish "main/fw_version" "`grep 'fw_version=' /etc/libiio.ini | sed s,fw_version=,,`"
}

# Parse data provided on mqtt and execute commands
parse_cmd () {
  cmd="${1//cmd\//}"  
  shift       
  val="${@}"
  echo ${val} > ${rVMAP[$cmd]}
}   

# Watch files and publish information to multiple outputs

# Initial dump of data
dump_data

# Observer changes in files
inotifywait --format %w%f -r -q -m -e create -e modify ${folder}* | update_data &

# Watch files and publish information to multiple outputs
/usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "cmd/#" | while read i; do parse_cmd $i ; done &

# Dump data every 10 seconds
while true; do
  sleep 10;
  dump_data;
done
