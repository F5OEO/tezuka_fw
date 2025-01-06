#!/bin/bash
sudo openocd -f pluto.cfg &
sleep 2
{ 
    echo "targets zynq.cpu0";
    echo "halt";
echo "ps7_debug";
echo "ps7_init";
sleep 10
echo "ps7_post_config";
echo "poll on"
echo "load_image u-boot.elf"
sleep 15
echo "resume 0x04000000"
echo "exit"
} | telnet localhost 4444
