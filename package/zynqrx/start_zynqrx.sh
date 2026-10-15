#!/bin/sh
# Start the zynqrx DVB-S2 receiver in the background.
#
# Input comes from the AD936x through libiio, control/status go to the local
# MQTT broker, and the received transport stream is sent as UDP multicast and
# served over WebSocket at ws://<board>:8080/stream.ts (Dashboard > Webtioune).
#
# Without --start zynqrx only serves MQTT and waits for cmd/longmynd/process
# start (status on dt/longmynd/process). AUTOSTART=1 (default) passes --start so
# the receiver runs as soon as this script has launched it; AUTOSTART=0 leaves
# it stopped, for the Dashboard / MQTT to start.
#
# Usage: start_zynqrx [extra zynqrx rx options...]   (later options win)

ZYNQRX=${ZYNQRX:-/usr/bin/zynqrx}
FREQ=437
RS=0.333
UDP=230.0.0.3:10000
WS_PORT=8080
AUTOSTART=${AUTOSTART:-1}
LOG=/tmp/zynqrx.log

# Only one receiver can own the radio.
killall zynqrx 2>/dev/null
sleep 1

START=
[ "$AUTOSTART" = 1 ] && START=--start

# --std 2: DVB-S2 only, otherwise failed hunts fall into the slow DVB-S Viterbi.
nohup nice -n -10 "$ZYNQRX" rx --iio --mqtt 127.0.0.1:1883 \
	--freq "$FREQ" --rs "$RS" --std 2 --mods qpsk,8psk --ccm \
	--udp "$UDP" --ws "$WS_PORT" $START "$@" \
	> "$LOG" 2>&1 < /dev/null &

echo "zynqrx started (pid $!), log: $LOG, stream: ws://$(hostname):$WS_PORT/stream.ts"
