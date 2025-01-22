Welcome to Tezuka !
Welcome to Tezuka !

Test your firmware on RAM (before adopting it)

- Get the pluto.dfu
- ssh on pluto (192.168.2.1 or pluto.local) (login:root, passwd:analog) and type

fw_setenv ramboot_verbose 'adi_hwref;echo Copying Linux from DFU to RAM... && run dfu_ram;if run adi_loadvals; then echo Loaded AD936x refclk frequency and model into devicetree; fi; envversion;setenv bootargs console=ttyPS0,115200 maxcpus=${maxcpus} rootfstype=ramfs root=/dev/ram0 rw earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=uio_pdrv_genirq uboot="${uboot-version}" && bootm ${fit_load_address}#${fit_config}'

pluto_reboot ram

- Your pluto is now on dfu ram waiting for a firmware to flash on ram
- In folder you place your pluto.dfu
sudo dfu-util -d 0456:b673,0456:b674 -D ./pluto.dfu -a firmware.dfu -R

- Tezuka firmware should be there ! 
- ssh to pluto and check that you have a banner

Welcome to Tezuka
pluto login: root
Password: 

___________                  __            
\__    ___/___ __________ __|  | _______   
  |    |_/ __ \\___   /  |  \  |/ /\__  \  
  |    |\  ___/ /    /|  |  /    <  / __ \_
  |____| \___  >_____ \____/|__|_ \(____  /
             \/      \/          \/     \/ 
Universal Zynq70x/AD936x firmware builder 
Version tezuka-bf07
By F5OEO (2024)

- Open you web browser and test http://192.168.2.1:8000/

