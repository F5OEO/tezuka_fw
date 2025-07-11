#!/bin/bash

SERIAL_PORT="/dev/ttyACM0"

# declare folder, files and their corresponding aliases (seen by client)

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

# based on VMAP, declare rVMAP
declare -A rVMAP=(
  [rx/gain]=${folder}in_voltage0_hardwaregain
  
)
#for K in "${!VMAP[@]}"; do rVMAP[${VMAP[$K]}]=$K; done

# Publish to mqtt
mqtt_publish () {
  /usr/bin/mosquitto_pub -r -i "tezuka_pub" -t "state/${1}" -m "${2}"
}

# Publish to serial port.
serial_publish () {
  if [ -e "${SERIAL_PORT}" ]; then
		echo "${1}" "${2}" > "${SERIAL_PORT}"
  fi
  
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
  while read j; do
    k=${VMAP[$j]};
    v="`read_file $j`";
    publish ${k} ${v};
  done
}

declare -x sweep_frequency=200000000
#export sweep_frequency
#sweep_span=480000000
#sweep_on=0

# Collect all settings we want to report
dump_data () {
  for K in "${!VMAP[@]}"; do publish ${VMAP[$K]} "`read_file $K`"; done
  for K in "${!cVMAP[@]}"; do publish ${cVMAP[$K]} "`read_file $K`"; done
  publish "main/serial" "`read_file /etc/serial`"
  publish "main/hw_model" "`grep 'hw_model=' /etc/libiio.ini | sed s,hw_model=,,`"
  publish "main/fw_version" "`grep 'fw_version=' /etc/libiio.ini | sed s,fw_version=,,`"
  sweep=$(iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable)

  if [ $sweep == "1" ]; then
    publish "rx/sweep/activate" "1"
    
  else
    publish "rx/sweep/activate" "0"
  fi
  echo  $sweep > /tmp/sweep_on
  sweep_frequency=$(cat /tmp/sweep_frequency)
  sweep_span=$(cat /tmp/sweep_span)
  
  publish "rx/sweep/frequency" $sweep_frequency
  publish "rx/span" $sweep_span

  rx_overload=$(iio_attr -D ad9361-phy direct_reg_access 0x5E)
  rx_overload=$((rx_overload & 1))
  if [ $rx_overload == "1" ]; then
    publish "rx/overload" "1"
  else
    publish "rx/overload" "0"
  fi

}

#frequency,span,on

update_sweep () {
   
      /root/sweep.sh $1 $2 $3
   
}

# Parse data provided on mqtt and execute commands
parse_cmd () {
  
  cmd="${1//cmd\//}"  
  shift       
  val="${@}"
  #echo ${val} > ${rVMAP[$cmd]}
  #First check if it is a classical iio cmd
  if  [ ${rVMAP[$cmd]} ]; then
      #echo "Cmd known ${cmd} ${val}"  
      echo ${val} > ${rVMAP[$cmd]} &
  else
    echo ${cmd} ${val}
    case $cmd in
    rx/rfinput)
      /root/switch_rfinput.sh ${val} &
    ;;
    rx/span)
      SPAN=$(printf "%0.f" $val)
     
      echo $SPAN > /tmp/sweep_span
      if [ "$SPAN" -gt "60000000" ]; then
        current_frequency=$(cat ${folder}out_altvoltage0_RX_LO_frequency)
         update_sweep $current_frequency $SPAN 1        
      else
        /root/sweep_stop.sh
        echo 60000000 > ${folder}in_voltage_sampling_frequency

      fi
       
    ;;
    rx/frequency)
      
      SPAN=$(cat /tmp/sweep_span)
      sweep_on=$(cat /tmp/sweep_on)
      echo $val > /tmp/sweep_frequency

      if [ "$sweep_on" == "1" ]; then
        
         update_sweep $val $SPAN 1        
      else
        echo $val > ${folder}out_altvoltage0_RX_LO_frequency

      fi
       
    ;;
    rx/sweep/frequency)
    if [ "$sweep_frequency" != "$val" ]; then
    sweep_frequency=$val
    echo $sweep_frequency > /tmp/sweep_frequency
    echo "Sweep Freqency $sweep_frequency"
    update_sweep $sweep_frequency $sweep_span $sweep_on 
    fi
    ;;
    rx/sweep/span)
    if [ "$sweep_span" != "$val" ] ; then
    sweep_span=$val
    echo $sweep_span > /tmp/sweep_span
    echo "Sweep span $val"
    update_sweep $sweep_frequency $sweep_span $sweep_on 
    fi
    ;;
    rx/sweep/activate)
    sweep_on=$(cat /tmp/sweep_on)
    if [ "$val" == "1" ]; then
    sweep_on=$val
    update_sweep $sweep_frequency $sweep_span $sweep_on 
    else
    /root/sweep_stop.sh
    fi
    ;;
    esac
      
  fi  
}   

# Watch files and publish information to multiple outputs

# Initial dump of data
#dump_data

# Observer changes in files
#inotifywait --format %w%f -r -q -m -e create -e modify ${folder}* | update_data &

# Watch files and publish information to multiple outputs
/usr/bin/mosquitto_sub -v -i "tezuka_sub" -t "cmd/#" | while read i; do parse_cmd $i ; done &

# Dump data every 10 seconds

while true; do

  #update_data
  
  dump_data
  sleep 2
done
