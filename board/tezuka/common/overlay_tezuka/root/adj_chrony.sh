precision_threshold="0.8"

while :
do
precision=$(chronyc tracking | grep Skew | cut -d ":" -f 2 | cut -d " " -f 2)
st=`echo "$precision < $precision_threshold" | bc`
if [ $st -eq 1 ] ; then
ppm=$(chronyc tracking | grep Frequency | cut -d ":" -f 2 | cut -d " " -f 2)
sign_correction=$(chronyc tracking | grep Frequency | cut -d ":" -f 2 | cut -d " " -f 4)

echo "High accurate ppm = $ppm +/- $precision $sign_correction"
if [ "$sign_correction" = "fast" ] ; then
    xo=$(printf "%.0f" $(echo "scale=25; 40000000*(1-($ppm)/1000000)" | bc))
else
    xo=$(printf "%.0f" $(echo "scale=25; 40000000*(1+($ppm)/1000000)" | bc))
fi        
echo "Correct $xo"
echo $xo > /sys/bus/iio/devices/iio:device0/xo_correction

else
echo "Wait for chrony precision : current $precision"
fi
sleep 1
done
