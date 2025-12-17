

freq=110000000
decim=48
sr=61000000
rfbw=56000000
gain=50
fps=50

set_maia()
{
adress=$1
curl -X PATCH -H "Content-Type: application/json" -d '{"rx_lo_frequency":'$freq'}' "http://$adress:8000/api/ad9361"
curl -X PATCH -H "Content-Type: application/json" -d '{"sampling_frequency":'$sr'}' "http://$adress:8000/api/ad9361"
curl -X PATCH -H "Content-Type: application/json" -d '{"rx_rf_bandwidth":'$rfbw'}' "http://$adress:8000/api/ad9361"
curl -X PATCH -H "Content-Type: application/json" -d '{"rx_gain_mode":"Manual"}' "http://$adress:8000/api/ad9361"
curl -X PATCH -H "Content-Type: application/json" -d '{"rx_gain":'$gain'}' "http://$adress:8000/api/ad9361"

curl -X PATCH -H "Content-Type: application/json" -d '{"output_sampling_frequency":'$fps'}' "http://$adress:8000/api/spectrometer"


curl -X PATCH -H "Content-Type: application/json" -d '{"frequency":'0.0'}' "http://$adress:8000/api/ddc/config"
curl -X PUT -H "Content-Type: application/json" -d '{"frequency":0.0,"decimation":'$decim',"transition_bandwidth":0.05,"passband_ripple":0.01,"stopband_attenuation_db":40.0,"stopband_one_over_f":true}' "http://$adress:8000/api/ddc/design"
}

for i in plutoplus.local fishball.local libresdr.local antsdre200.local
do
    set_maia $i
done
