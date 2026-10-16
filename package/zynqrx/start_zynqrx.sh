#!/bin/sh
# Start the zynqrx DVB-S2 receiver in the background.
#
# Input comes from the AD936x through libiio, control/status go to the local
# MQTT broker, and the received transport stream is sent as UDP multicast and
# served over WebSocket at ws://<board>:8080/stream.ts (Dashboard > Webtioune).
#
# Without --start zynqrx only serves MQTT and waits for cmd/zynqrx/process
# start (status on dt/zynqrx/process). AUTOSTART=1 (default) passes --start so
# the receiver runs as soon as this script has launched it; AUTOSTART=0 leaves
# it stopped, for the Dashboard / MQTT to start.
#
# Usage: start_zynqrx [extra zynqrx rx options...]   (later options win)

ZYNQRX=${ZYNQRX:-/usr/bin/zynqrx}
FREQ=437
RS=0.333
UDP=230.0.0.3:1234
WS_PORT=8080
AUTOSTART=${AUTOSTART:-1}
LOG=/tmp/zynqrx.log

# Only one receiver can own the radio. Wait until the old one has really gone (it can take seconds when it is busy hunting): a new one started
# while the old still holds the WebSocket port fails to bind it and exits at once.
killall zynqrx 2>/dev/null
i=0
while pidof zynqrx >/dev/null 2>&1 && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
if pidof zynqrx >/dev/null 2>&1; then killall -9 zynqrx 2>/dev/null; sleep 1; fi

START=
[ "$AUTOSTART" = 1 ] && START=--start

# --std 2: DVB-S2 only, otherwise failed hunts fall into the slow DVB-S Viterbi.
# --no-bch: skip the BCH stage (it never corrected anything in tests). --iq-format cs8: 8-bit samples from the FPGA (equal to cs16 under the
# radio AGC, but 24 dB worse at a low fixed gain: pass --iq-format cs16 with --gain). --ldpc8: the 8-bit LDPC decoder at every code rate.
nohup nice -n -10 "$ZYNQRX" rx --iio --mqtt 127.0.0.1:1883 \
	--freq "$FREQ" --rs "$RS" --std 2 --mods qpsk,8psk --ccm --no-bch --iq-format cs8 --ldpc8 \
	--udp "$UDP" --ws "$WS_PORT" $START "$@" \
	> "$LOG" 2>&1 < /dev/null &

sleep 1
if ! kill -0 $! 2>/dev/null; then echo "zynqrx did not start, see $LOG" >&2; exit 1; fi
echo "zynqrx started (pid $!), log: $LOG, stream: ws://$(hostname):$WS_PORT/stream.ts"
