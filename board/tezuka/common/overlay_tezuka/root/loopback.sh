#!/bin/sh


# TX
echo 2 > /sys/kernel/debug/iio/iio:device0/loopback
echo 0 > /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown
echo 2500000 > /sys/bus/iio/devices/iio:device0/out_voltage_rf_bandwidth
echo "-30" > /sys/bus/iio/devices/iio:device0/out_voltage0_hardwaregain
echo 950000000 > /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_frequency

# RX
echo 2500000 > /sys/bus/iio/devices/iio:device0/in_voltage_sampling_frequency
echo 2395000000 > /sys/bus/iio/devices/iio:device0/out_altvoltage0_RX_LO_frequency
echo 2500000 > /sys/bus/iio/devices/iio:device0/in_voltage_rf_bandwidth
