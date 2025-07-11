#!/bin/sh
#https://www.analog.com/media/cn/technical-documentation/user-guides/AD9364_Register_Map_Reference_Manual_UG-672.pdf

adphys="$(cat /sys/bus/iio/devices/iio:device0/name)"

getoriginal()
{
     if [ "$adphys" = "ad9361-phy" ] ; then

                echo 0x3 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device0/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
                mode="$(cat /sys/kernel/debug/iio/iio:device0/adi,2rx-2tx-mode-enable)"
        else

                echo 0x3 > /sys/kernel/debug/iio/iio:device1/direct_reg_access
                CURRENT_VALUE=$(cat  /sys/kernel/debug/iio/iio:device1/direct_reg_access | grep -o '0x[0-9a-fA-F]\+')
                mode="$(cat /sys/kernel/debug/iio/iio:device1/adi,2rx-2tx-mode-enable)"
        fi
}

setnewvalue()
{
     # Convert the current value from hex to decimal
        CURRENT_VALUE_DEC=$((CURRENT_VALUE))

        # Clear the bits corresponding to the PARAM_VALUE before setting the new value
        CLEAR_MASK=$((~0xC0))
        CURRENT_VALUE_CLEARED=$((CURRENT_VALUE_DEC & CLEAR_MASK))

        # Perform bitwise OR with the parameter value
        RESULT_VALUE=$((CURRENT_VALUE_CLEARED |$1))

        # Convert the result back to hex
        RESULT_VALUE_HEX=$(printf "0x%X" $RESULT_VALUE)
}

rx1()
{
    
        # Get the current register value
        if [ "$adphys" = "ad9361-phy" ] ; then
        
               echo 1 >/sys/kernel/debug/iio/iio:device0/adi,1rx-1tx-mode-use-rx-num
               setnewvalue 64
               echo 0x3 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device0/direct_reg_access 
                
        else
        #REVB no RF on rx2
               echo 1 >/sys/kernel/debug/iio/iio:device1/adi,1rx-1tx-mode-use-rx-num
               setnewvalue 64
               echo 0x3 $RESULT_VALUE_HEX > /sys/kernel/debug/iio/iio:device1/direct_reg_access 
               
        fi
 

}

rx2()
{
    
        # Get the current register value
        if [ "$adphys" = "ad9361-phy" ] ; then
               echo 2 >/sys/kernel/debug/iio/iio:device0/adi,1rx-1tx-mode-use-rx-num
               setnewvalue 128
               echo 0x3 $RESULT_VALUE_HEX> /sys/kernel/debug/iio/iio:device0/direct_reg_access 
                
        else
        #REVB no RF on rx2
               echo 2 >/sys/kernel/debug/iio/iio:device1/adi,1rx-1tx-mode-use-rx-num
               setnewvalue 128
               echo 0x3 $RESULT_VALUE_HEX> /sys/kernel/debug/iio/iio:device1/direct_reg_access 
               
        fi
 

}

getoriginal
if [ "$mode" = "0" ] ; then
        if [ "$1" = "rx2" ] ; then
        rx2
        else
        rx1
        fi
else
echo "Switch rf no relevant as we are in 2R2T"
fi