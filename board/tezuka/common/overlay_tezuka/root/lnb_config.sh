#!/bin/sh


# Convert parameter to corresponding value
lnb_setting=$(fw_printenv -n lnb_power)

#Sleep in order to have manual gpio
sleep 0.5

 case $lnb_setting in
    off)
        $(/root/switch_lnb.sh $lnb_setting)
        ;;
    13V)
        $(/root/switch_lnb.sh $lnb_setting)
        ;;
    18V)
        $(/root/switch_lnb.sh $lnb_setting)
        ;;
    auto)
        
        ;;
    *)
        echo "Invalid parameter. Use one of: off, 13V, 18V" >> /tmp/lnb.txt
        exit 1
        ;;
esac

