#!/bin/sh
decim=$1
curl -X PUT -H "Content-Type: application/json" -d '{"frequency":0.0,"decimation":'$decim',"transition_bandwidth":0.05,"passband_ripple":0.01,"stopband_attenuation_db":60.0,"stopband_one_over_f":true}' "http://localhost:8000/api/ddc/design"

devmem 0x790200BC 32 1
