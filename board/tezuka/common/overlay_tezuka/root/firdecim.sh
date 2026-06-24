#!/bin/sh
# Manual DDC helper: design the decimating FIR and route the DDC output into the
# cf-ad9361-lpc capture device, both via maia-httpd REST on port 80.
#
#   firdecim.sh <decimation> [on|off]
#     on  (default) : design FIR for <decimation> and enable DDC routing
#     off           : restore full-rate wideband IQ (disable routing)
#
# Routing is default-OFF at boot (nothing sets it), so ordinary libiio clients
# (SDR++/SDR#/GNU Radio) get full IQ until this (or an API client like OpenRF)
# enables it. Previously this PUT a dead :8000 and poked devmem 0x790200BC directly;
# it now goes through PATCH /api/ddc/config {enabled} so the state stays consistent.
decim=$1
state=${2:-on}
api="http://localhost:80/api"

if [ -z "$decim" ]; then
  echo "usage: $0 <decimation> [on|off]" >&2
  exit 1
fi

curl -fsS -X PUT -H "Content-Type: application/json" \
  -d '{"frequency":0.0,"decimation":'"$decim"',"transition_bandwidth":0.05,"passband_ripple":0.01,"stopband_attenuation_db":60.0,"stopband_one_over_f":true}' \
  "$api/ddc/design"

if [ "$state" = "off" ]; then en=false; else en=true; fi
curl -fsS -X PATCH -H "Content-Type: application/json" \
  -d '{"enabled":'"$en"'}' \
  "$api/ddc/config"
