while :
do
change=$(inotifywait -e modify --format "%w/%f" /sys/bus/iio/devices/iio\:device*) 2>/dev/null
value=$(cat $change)
printf "ExternSDR: %s -> %s\n" "$change" "$value"
done
