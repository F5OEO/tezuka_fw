cor="0.85"
while :
do
status=$(ntptime | grep status | cut -d " " -f 4)
if [ "$status" = "0x2001" ] ; then
ppm=$(ntptime | grep frequency | cut -d " " -f 7)
echo "High accurate ppm = $ppm"

xo=$(printf "%.0f" $(echo "scale=25; 40000000*(1-($ppm+$cor)/1000000)" | bc))
echo "Correct $xo"
echo $xo > /sys/bus/iio/devices/iio:device0/xo_correction

else
if [ "$status" = "0x6001" ] ; then
ppm=$(ntptime | grep frequency | cut -d " " -f 7)
echo "Low accurate ppm = $ppm"
xo=$(printf "%.0f" $(echo "scale=25; 40000000*(1-($ppm+$cor)/1000000)" | bc))
echo "Correct $xo"
echo $xo > /sys/bus/iio/devices/iio:device0/xo_correction


else
echo "Wait for ntp pll"
fi
sleep 1
done
