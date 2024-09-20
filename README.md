# tezuka_fw / Buildroot builder

Universal Zynq/AD9363 firmware builder : PlutoSDR, Pluto+, AntSDR (e200). Other targets could be added (ZynqSDR,LibreSDR..)

## Install once
wget https://buildroot.org/downloads/buildroot-2024.08.tar.gz
tar -xvf buildroot-2024.08.tar.gz

## Build
git clone https://github.com/F5OEO/tezuka_fw
cd tezuka_fw
source sourceme.first
in your buildroot folder :
make zynq_pluto_universal_defconfig
make

## Result
All materials are in buildroot/output/images






