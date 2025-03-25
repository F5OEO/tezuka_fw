while :
do

read_register=$(devmem 0x41210000)
#echo "Reg $read_register"
ten_mhz=$((($read_register>>1) & 0x1))
if [ "$ten_mhz" = "1" ] ; then
        dev=$((($read_register >> 4) & 0xFFFF))
        signed=$((dev>>15))
        if [ "$signed" = "1" ] ; then
                dev=$(($dev | 0xFFFFFFFFFFFF8000))
        fi
#       echo "Ten $ten_mhz"
#       printf "Dev %d %s\n" $dev $signed
        correct=$((dev * 2))
        final_xo=$((40000000-$correct))
        echo "Xo Correction $final_xo"
        echo $final_xo > /sys/bus/iio/devices/iio:device0/xo_correction
fi
sleep 1
done
