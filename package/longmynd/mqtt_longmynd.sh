key=$(fw_printenv -n call)
dt_root="dt/pluto/$key"
cmd_root="cmd/pluto/$key"
paramlongmynd="-r -1 -p h -M 127.0.0.1 1883 -i 230.0.0.2 1234 741500 1500"

while :
do
param=$(mosquitto_sub -t $cmd_root/system/longmynd -C 1)
killall -9 longmynd
[ "$param" == "on" ] && /usr/bin/longmynd $paramlongmynd &

done
