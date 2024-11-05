![tezuka banner](/doc/tezuka.png)
# About tezuka_fw 
**tezuka** (name referenced to pluto) aims to be Universal Zynq/AD9363 firmware builder for plutosdr board and other boards: PlutoSDR, Pluto+, AntSDR (e200). Other targets could be added (ZynqSDR,LibreSDR..)

# Why not an Analog Device firmware ?
ADI launch PlutoSDR as a learning platform and it is ! But for since firmware 0.38, it is mainly update to work with https://wiki.analog.com/resources/eval/user-guides/circuits-from-the-lab/cn0566

Even this is a very smart product, the price is out of most of hobbists and hamradio people.

Target of **tezuka** firmware is to maximize functionnalities of the board and integrate interesting projects on multi-target boards.

It could encourage people to make new firmware more easily without to fork and clone large submodules.

# New features
- Frequencies extension : **47.5Mhz**-6Ghz
- Switch RX1/RX2 , TX1/TX2 seamlessly
- Complex 8bit mode to extend streaming bandwidth with host
- Audio gadget to be recognized as a soundcard
- USB boot support
- SD boot support
- 
# Third party software which could use extra features
- Satdump
- sdr++
- Soapy based software

You maybe need to use forks of these software until it is integrated in the mainline of these projects.

- https://github.com/F5OEO/SatDump
- https://github.com/F5OEO/SDRPlusPlus
- https://github.com/F5OEO/SoapyPlutoPAPR

# Firmwares available
## Maiasdr
https://maia-sdr.org/

# Building from source (linux debian based)
## Install once
```
git clone https://github.com/F5OEO/tezuka_fw
cd tezuka_fw
wget https://buildroot.org/downloads/buildroot-2024.08.tar.gz
tar -xvf buildroot-2024.08.tar.gz
```
## Build
```
source sourceme.first
cd buildroot-2024.08
make zynq_pluto_original_defconfig && make
```
## Result
All materials are in buildroot/output/images






