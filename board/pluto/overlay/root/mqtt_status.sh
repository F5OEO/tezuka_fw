#!/bin/sh


prefix='/sys/bus/iio/devices/iio:device0/'

sources="out_voltage0_rf_port_select in_voltage0_rf_port_select out_altvoltage1_TX_LO_frequency out_altvoltage0_RX_LO_frequency out_voltage_rf_bandwidth in_voltage_rf_bandwidth xo_correction out_voltage_sampling_frequency in_voltage_sampling_frequency in_voltage0_gain_control_mode in_voltage0_hardwaregain out_voltage0_hardwaregain"

while true; do

  for i in $sources; do

    /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "status/$i" -s < $prefix$i

  done
  
  sleep 0.1

done
