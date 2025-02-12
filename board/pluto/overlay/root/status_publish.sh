#!/bin/bash

# declare folder, files and their corresponding aliases (seen by client)

folder='/sys/bus/iio/devices/iio:device0/'

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


# based on VMAP, declare rVMAP - so we can search both directions and vVMAP so we can map real values

declare -A rVMAP
declare -A vVMAP
for K in "${!VMAP[@]}"; do rVMAP[${VMAP[$K]}]=$K; done

# Functions to handle the data

mqtt_publish () {
  /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "dt/$1" -m "$2"
}

serial_publish () {
  # for future use to publish to a serial port
  echo $1 $2
}

publish () {                                                       
  mqtt_publish $1 $2
  serial_publish $1 $2
}                                                             

update_data () {
  while read i; do
    k=${VMAP[$i]};
    v="`cat $i`";
    publish $k $v;
  done
}

dump_data () {
  for K in "${!VMAP[@]}"; do publish ${VMAP[$K]} "`cat $K`"; done
  publish "main/serial" "`cat /etc/serial`"
}


# Watch files and publish information to multiple outputs

dump_data

inotifywait --format %w%f -r -q -m -e create -e modify ${folder}* | update_data &

while true; do
  sleep 10;
  dump_data;
done
