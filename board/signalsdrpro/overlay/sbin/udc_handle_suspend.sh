#!/bin/sh

source /etc/device_config

DEV_NAME=

for dev in /sys/bus/iio/devices/*; do
	[ `cat ${dev}/name` == "ad9361-phy" ] && DEV_NAME=`basename ${dev}`
done

prev_ensm_mode=`cat /sys/bus/iio/devices/${DEV_NAME}/ensm_mode`

while [ 1 ]
do

state=`/sbin/poll_sysfs /sys/class/udc/ci_hdrc.0/state`
case "$state" in
	suspended)
		prev_ensm_mode=`cat /sys/bus/iio/devices/${DEV_NAME}/ensm_mode`
		echo sleep > /sys/bus/iio/devices/${DEV_NAME}/ensm_mode
		echo none > /sys/class/leds/led0:green/trigger
		echo "UDC: $state: $prev_ensm_mode -> sleep" | logger
		;;
	*)
		current_ensm_mode=`cat /sys/bus/iio/devices/${DEV_NAME}/ensm_mode`
		echo heartbeat > /sys/class/leds/led0:green/trigger
		if [ "$current_ensm_mode" != "$prev_ensm_mode" ]
		then
		  echo $prev_ensm_mode > /sys/bus/iio/devices/${DEV_NAME}/ensm_mode
		fi
		echo "UDC: $state: $current_ensm_mode -> $prev_ensm_mode" | logger
		;;
esac
done
