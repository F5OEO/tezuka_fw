#sshpass -p analog ssh root@pluto.local '/usr/sbin/pluto_reboot ram;'
#sleep 5
sudo dfu-util -d 0456:b673,0456:b674 -D ./build/pluto.dfu -a firmware.dfu -R

