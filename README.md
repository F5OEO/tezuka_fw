![tezuka banner](/doc/tezuka.png)
# About tezuka_fw 
**tezuka** (name referenced to [pluto](https://en.wikipedia.org/wiki/Pluto:_Urasawa_x_Tezuka)) aims to be Universal Zynq/AD9363 firmware builder for plutosdr board and other boards: PlutoSDR, Pluto+, AntSDR (e200). Other targets could be added (ZynqSDR,LibreSDR..)

# Why not an Analog Device firmware ?
ADI launch PlutoSDR as a learning platform and it is ! But since 2 years, updates are mainly focused on supporting $$$phaser product https://wiki.analog.com/resources/eval/user-guides/circuits-from-the-lab/cn0566

Even this is a very smart product, the price is out of most of hobbists and hamradio people (around 2500 Euros).

Target of **tezuka** firmware is to **maximize functionnalities** of the board and integrate interesting projects on multi-target boards.

It could encourage people to make new firmware more easily without to fork and clone large submodules.

# New features
- Frequencies extension : **47.5Mhz**-6Ghz
- Switch **RX1/RX2 , TX1/TX2** seamlessly
- Complex **8bit mode** to extend streaming bandwidth with host (**14Mhz** stable bandwidth through usb)
- Audio gadget to be recognized as a soundcard (**virtual cable** not needed anymore)
- USB boot support ( allows heavy package like GnuRadio to be embedded)
- SD boot support - same as above but on SD capable plateform (pluto+, anstdr)

# Installing
 - Download your selected configuration on https://github.com/F5OEO/tezuka_fw/releases
 - Unzip it and paste pluto.frm to pluto drive (detailed update procedure https://wiki.analog.com/university/tools/pluto/users/firmware)

# Configuring
A soon as firmware is updated, you could see a usb drive with parameters in config.txt (orginal parameters are described at (https://wiki.analog.com/university/tools/pluto/users/customizing)

# Third party software which could use extra features
- Satdump (https://github.com/F5OEO/SatDump)
- sdr++ (https://github.com/F5OEO/SDRPlusPlus)
- Soapy based software(https://github.com/F5OEO/SoapyPlutoPAPR)

Other great SDR software could use soon new features (SDRConsomle, SDRAngel...), stay tuned !

# Firmwares available
- In release section (https://github.com/F5OEO/tezuka_fw/releases)
- Choose your right firmware depending on hardware and software, depending on name.
- Firmware with name maia is referenced to https://maia-sdr.org/ amazing project which is included with a **normal** behavior regarding to third party softwares.

# For developpers
## Building from source (linux debian based)
### Install once
```
git clone https://github.com/F5OEO/tezuka_fw
cd tezuka_fw
wget https://buildroot.org/downloads/buildroot-2024.08.tar.gz
tar -xvf buildroot-2024.08.tar.gz
```
### Build
```
source sourceme.first
cd buildroot-2024.08
make zynq_pluto_original_defconfig && make
```
### Result
All materials are in buildroot/output/images






