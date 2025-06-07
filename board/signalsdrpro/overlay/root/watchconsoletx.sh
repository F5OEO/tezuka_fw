#!/bin/sh
#https://www.analog.com/media/cn/technical-documentation/user-guides/AD9364_Register_Map_Reference_Manual_UG-672.pdf

adphys="$(cat /sys/bus/iio/devices/iio:device0/name)"

ptton()
{
    #PTT on GPIO 0  (GPIO 1 should be not touched)
        # Get the current register value
        if [ "$adphys" = "ad9361-phy" ] ; then

                echo 0x27 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device0/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        else

                echo 0x27 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device1/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        fi
        
        if [ -z "$CURRENT_VALUE" ]; then
                echo "Failed to retrieve the current register value." >> /tmp/watch.txt
                exit 1
        fi

        # Convert the current value from hex to decimal
        CURRENT_VALUE_DEC=$((CURRENT_VALUE))

        # Clear the bits corresponding to the PARAM_VALUE before setting the new value
        CLEAR_MASK=$((~0x10))
        CURRENT_VALUE_CLEARED=$((CURRENT_VALUE_DEC & CLEAR_MASK))

        # Perform bitwise OR with the parameter value
        RESULT_VALUE=$((CURRENT_VALUE_CLEARED |0x10))

        # Convert the result back to hex
        RESULT_VALUE_HEX=$(printf "0x%X" $RESULT_VALUE)

        # Pass the result to the direct register access, keeping the address 0x27

        #echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device0/direct_reg_access
    if [ "$adphys" = "ad9361-phy" ] ; then
                echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device0/direct_reg_access
    else
                echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device1/direct_reg_access
    fi

    echo 1 > /sys/class/gpio/gpio906/value
    echo "$(date) watchconsoleTX PTT_ON" >> /tmp/lnb.txt    



}

pttoff()
{
    # Get the current register value
               if [ "$adphys" = "ad9361-phy" ] ; then

                echo 0x27 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device0/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        else

                echo 0x27 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device1/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
        fi


        if [ -z "$CURRENT_VALUE" ]; then
                 echo "Failed to retrieve the current register value." >> /tmp/watch.txt
                exit 1
        fi

        # Convert the current value from hex to decimal
        CURRENT_VALUE_DEC=$((CURRENT_VALUE))

        # Clear the bits corresponding to the PARAM_VALUE before setting the new value
        CLEAR_MASK=$((~0x10))
        CURRENT_VALUE_CLEARED=$((CURRENT_VALUE_DEC & CLEAR_MASK))

        # Perform bitwise OR with the parameter value
        RESULT_VALUE=$((CURRENT_VALUE_CLEARED |0x00))

        # Convert the result back to hex
        RESULT_VALUE_HEX=$(printf "0x%X" $RESULT_VALUE)

        # Pass the result to the direct register access, keeping the address 0x27
        #echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device0/direct_reg_access


    if [ "$adphys" = "ad9361-phy" ] ; then
            echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device0/direct_reg_access
    else
        echo "0x27 $RESULT_VALUE_HEX" > /sys/kernel/debug/iio/iio:device1/direct_reg_access
    fi
    echo 0 > /sys/class/gpio/gpio906/value
    echo "$(date) watchconsoleTX PTT_OFF" >> /tmp/lnb.txt
}


if [ "$adphys" = "ad9361-phy" ] ; then
        echo manual_tx_quad > /sys/bus/iio/devices/iio:device0/calib_mode
        #Manual GPIO
        echo 0x26 0x10 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
else
        echo manual_tx_quad > /sys/bus/iio/devices/iio:device1/calib_mode
        #Manual GPIO
        echo 0x26 0x10 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
fi

#MIO for plutoplus : MIO start at 906, EMIO at 960
echo "906" > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio906/direction

pttoff

loop()
{


while :
do
if [ "$adphys" = "ad9361-phy" ] ; then
inotifywait -e modify /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown
txmute=$(cat /sys/bus/iio/devices/iio:device0/out_altvoltage1_TX_LO_powerdown)
else
inotifywait -e modify /sys/bus/iio/devices/iio:device1/out_altvoltage1_TX_LO_powerdown
txmute=$(cat /sys/bus/iio/devices/iio:device1/out_altvoltage1_TX_LO_powerdown)
fi
if [ "$txmute" = "1" ] ; then
echo "SdrConsole PTT OFF"
pttoff
else

   echo "SdrConsole PTT ON"
   ptton
fi
done
}

loop
