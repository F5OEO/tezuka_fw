#!/bin/sh
#https://www.analog.com/media/cn/technical-documentation/user-guides/AD9364_Register_Map_Reference_Manual_UG-672.pdf

adphys="$(cat /sys/bus/iio/devices/iio:device0/name)"
lnb_mode=$(fw_printenv -n lnb_power)

loop()
{


while :
do
if [ "$adphys" = "ad9361-phy" ] ; then
inotifywait -e modify /sys/bus/iio/devices/iio:device0/out_altvoltage0_RX_LO_frequency
rxfrequency=$(cat /sys/bus/iio/devices/iio:device0/out_altvoltage0_RX_LO_frequency)
else
inotifywait -e modify /sys/bus/iio/devices/iio:device1/out_altvoltage0_RX_LO_frequency
rxfrequency=$(cat /sys/bus/iio/devices/iio:device1/out_altvoltage0_RX_LO_frequency)
fi

#echo "FreqRX $rxfrequency"

if [ "$lnb_mode" = "auto" ] ; then

        if [ "$rxfrequency" -le "740500000" ] ; then
#        echo "QO100 Narrow Band"
        $(/root/switch_lnb.sh 13V)
        else
#        echo "QO100 Wide Band"
        $(/root/switch_lnb.sh 18V)
        fi
fi
done
}

loop
