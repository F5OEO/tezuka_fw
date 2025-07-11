#!/bin/sh

##################
# Read actual GPIO config and apply LNB setup for GPIO 2 & 3
# Get LNB config from 'lnb_power' NVRAM variable (fw_setenv/fw_printenv)
# Usage: lnb_config.sh
# Lamableu
#
#################
adphys="$(cat /sys/bus/iio/devices/iio:device0/name)"

        if [ "$adphys" = "ad9361-phy" ] ; then
                #Manual GPIO
                echo 0x26 0x10 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
                echo 0x27 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
                actual=$(cat  /sys/kernel/debug/iio/iio:device0/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        else
                #Manual GPIO
                echo 0x26 0x10 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
                echo 0x27 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
                actual=$(cat  /sys/kernel/debug/iio/iio:device1/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        fi

echo "Read actual config: ${actual}" >> /tmp/lnb.txt

# Convert parameter to corresponding value
case $1 in
    off)
        PARAM_VALUE=0x00
        #echo "LNB select: OFF/0x00"
        ;;
    13V)
        PARAM_VALUE=0x80
        #echo "LNB select: 13V/0x80"
        ;;
    18V)
        PARAM_VALUE=0xC0
        #echo "LNB select: 18V/0xC0"

        ;;
    *)
        echo "Invalid parameter. Use one of: off, 13V, 18V" >> /tmp/lnb.txt
        exit 1
        ;;
esac

# Get the current register value

CURRENT_VALUE=$actual
if [ -z "$CURRENT_VALUE" ]; then
    echo "Failed to retrieve the current register value." >> /tmp/lnb.txt
    exit 1
fi

# Convert the current value from hex to decimal
CURRENT_VALUE_DEC=$((CURRENT_VALUE))

# Clear the bits corresponding to the PARAM_VALUE before setting the new value
CLEAR_MASK=$((~0xC0))
CURRENT_VALUE_CLEARED=$((CURRENT_VALUE_DEC & CLEAR_MASK))

# Perform bitwise OR with the parameter value
RESULT_VALUE=$((CURRENT_VALUE_CLEARED | PARAM_VALUE))

# Convert the result back to hex
RESULT_VALUE_HEX=$(printf "0x%X" $RESULT_VALUE)


    #Sleep in order to have manual gpio
    sleep 0.5
    if [ "$adphys" = "ad9361-phy" ] ; then
        echo 0x27 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device0/direct_reg_access
        echo "Command executed: echo 0x27 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device0/direct_reg_access" >> /tmp/lnb.txt

    else
        echo 0x27 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device1/direct_reg_access
        echo "Command executed: echo 0x27 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device1/direct_reg_access" >> /tmp/lnb.txt

    fi

