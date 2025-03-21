#!/bin/sh
#https://www.analog.com/media/cn/technical-documentation/user-guides/AD9364_Register_Map_Reference_Manual_UG-672.pdf

adphys="$(cat /sys/bus/iio/devices/iio:device0/name)"


loop()
{


while :
do
if [ "$adphys" = "ad9361-phy" ] ; then
inotifywait -e modify /sys/bus/iio/devices/iio:device0/ensm_mode

ensmode=$(cat /sys/bus/iio/devices/iio:device0/ensm_mode)
else
inotifywait -e modify /sys/bus/iio/devices/iio:device1/ensm_mode
ensmode=$(cat /sys/bus/iio/devices/iio:device1/ensm_mode)
fi

if [ "$ensmode" = "rx" ] ; then

if [ "$adphys" = "ad9361-phy" ] ; then
    echo 1 > /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown    
else
    echo 1 > /sys/bus/iio/devices/iio:device1/out_altvoltage1_TX_LO_powerdown     
fi

if [ "$ensmode" = "tx" ] ; then
if [ "$adphys" = "ad9361-phy" ] ; then
    echo 0 > /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown    
else
    echo 0 > /sys/bus/iio/devices/iio:device1/out_altvoltage1_TX_LO_powerdown     
fi
fi


done
}

loop
