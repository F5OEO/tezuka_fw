satdump legacy live dvbs_test_udp /tmp/dvbs_out \
    --source plutosdr \
    --source_id 0 \
    --ip_address localhost \
    --samplerate 250000 \
    --frequency 437000000 \
    --gain 50 \
    --gain_mode 2 \
    --symbolrate 250000 \
    --rrc_alpha 0
 

For DVB-S2:
  satdump legacy live dvbs2_test /tmp/dvbs2_out \
    --source plutosdr \
    --ip_address localhost \
    --samplerate 6000000 \
    --frequency 1000000000 \
    --gain 50 \ 
    --gain_mode 2 \
    --symbolrate 2000000 \
    --modcod 11

gdb --args satdump legacy live dvbs_test /tmp/dvbs_out \
    --source plutosdr \
    --ip_address localhost \
    --samplerate 4000000 \
    --frequency 1000000000 \
    --gain 50 \
    --gain_mode 2 \
    --symbolrate 2000000
