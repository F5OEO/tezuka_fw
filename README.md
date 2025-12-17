# ⚠️ This is a Fork: GPIO2 PTT Mod

This is a modified version of [F5OEO's tezuka_fw](https://github.com/F5OEO/tezuka_fw) with **PTT moved from GPIO0 to GPIO2**.

## Why?

I damaged GPIO0 on my PlutoSDR and needed an alternative. This mod moves the PTT output to GPIO2 as a workaround. Sharing in case others run into the same issue.

## What Changed

Modified `board/tezuka/common/overlay_tezuka/root/watchconsoletx.sh` to use GPIO2 instead of GPIO0.

The script controls PTT by writing to AD9361 register `0x27` via the IIO debug interface. The change is in the bitmask:

| GPIO Pin | Bitmask | Register 0x27 Bit |
|----------|---------|-------------------|
| GPIO0    | `0x10`  | Bit 4             |
| GPIO2    | `0x40`  | Bit 6             |

**Diff:**
```diff
- CLEAR_MASK=$((~0x10))
+ CLEAR_MASK=$((~0x40))

- RESULT_VALUE=$((CURRENT_VALUE_CLEARED |0x10))
+ RESULT_VALUE=$((CURRENT_VALUE_CLEARED |0x40))
```

## Installation

### Option 1: Full Firmware (recommended)

**SD Card Boot:**
1. Download `tezuka.zip` from [Releases](https://github.com/pumatrax/tezuka_fw/releases)
2. Extract `sdimg/` folder contents to a FAT32-formatted SD card
3. Insert card and boot

**Flash to memory:**
1. Copy `pluto.frm` to the Pluto drive
2. Eject the drive
3. Wait for the flash to complete

### Option 2: Patch Method (keep existing firmware)

If you want to keep your current tezuka firmware and just patch the PTT behavior:

1. Copy the modified `watchconsoletx.sh` to `/mnt/jffs2/` on the Pluto

2. Create `/mnt/jffs2/autorun.sh` with the following contents:
   ```sh
   #!/bin/sh
   killall watchsdrconsole
   /mnt/jffs2/watchconsoletx.sh &
   ```

3. Make it executable:
   ```sh
   chmod +x /mnt/jffs2/autorun.sh
   ```

4. Reboot the Pluto

## Hardware

- ADALM-PlutoSDR

## Credits

- [F5OEO (Evariste)](https://github.com/F5OEO) for the original tezuka firmware and guidance on this fix
- 73 de KD2NFC

---

# Original tezuka_fw README

![tezuka banner](/doc/tezuka.png)

[![Tezuka](https://github.com/F5OEO/tezuka_fw/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/F5OEO/tezuka_fw/actions/workflows/main.yml)

# About tezuka_fw
**tezuka** (name referenced to [pluto](https://en.wikipedia.org/wiki/Pluto:_Urasawa_x_Tezuka)) aims to be Universal Zynq/AD9363 firmware builder for plutosdr board and other boards: PlutoSDR, Pluto+, AntSDR (e200), LibreSDR.

Target of **tezuka** firmware is to **maximize features** of the board and integrate interesting projects on multi-target boards.

# New features
- Frequencies extension : **47.5Mhz**-6Ghz
- Switch **RX1/RX2 , TX1/TX2** seamlessly
- Complex **8bit mode** to extend streaming bandwidth with host (**14Mhz** stable bandwidth through usb, **45MHz** through GbE network)
- Audio gadget to be recognized as a soundcard (**virtual cable** not needed anymore)
- SD boot support : easy update, no risk of flashing, high amount of memory
- Include **Maia-sdr** transparently
- Fast sweep
- Gpios handling (OpenSDRLab Pluto Sky)
- Publish basic information about the current state on local mqtt server
- Many other (need to be documented)

# Why not Analog Device or firmware ?
ADI launch PlutoSDR as a learning platform and it is ! But since 2 years, updates are mainly focused on expensive phaser (around 2500 Euros) product https://wiki.analog.com/resources/eval/user-guides/circuits-from-the-lab/cn0566

Thus, official firmware updates are no longer focus on new features for SDR enthusiastic people.

# Installing
- In release section (https://github.com/F5OEO/tezuka_fw/releases)
- Choose your right firmware depending on hardware and software, depending on name.
- Download your selected configuration on https://github.com/F5OEO/tezuka_fw/releases
- Unzip it.
- **DO NOT USE standard method of flashing with frm**
- Write SD card : Copy contents of sdimg folder to a fresh FAT32 card formated.
  
**ONLY FOR PLUTOSDR (no SD card) :**
- Flash on memory : paste pluto.frm to pluto drive and eject (detailed update procedure https://wiki.analog.com/university/tools/pluto/users/firmware)

# Disclaimer

Most of boards use non protected flash memory. Flashing could break your card. **Until you know what you are doing, always boot in SD mode.** 

# Configuring
A soon as firmware is updated, you could see a usb drive with parameters in config.txt (orginal parameters are described at (https://wiki.analog.com/university/tools/pluto/users/customizing)

# Third party software which could use extra features
- Satdump (https://github.com/F5OEO/SatDump)
- sdr++ (https://github.com/F5OEO/SDRPlusPlus)
- Soapy based software(https://github.com/F5OEO/SoapyPlutoPAPR)

Other great SDR software could use soon new features (SDRConsole, SDRAngel...), stay tuned !

# Calling for contribution
If you like this firmware you can help me maintaining it by
- Donate at https://paypal.me/f5oeodev
- Write some documentation
- Make some pull request


# For developers
## Building from source (linux Debian based)
### Install once
#### Add required packages
Buildroot documentation has the [list of required packages](https://buildroot.org/downloads/manual/manual.html#requirement-mandatory).

The following packages must be installed for building Maia-fw related code:
```bash
sudo apt install pkg-config libssl-dev libclang-dev
```

Now clone this repo and get buildroot
```bash
git clone https://github.com/F5OEO/tezuka_fw
cd tezuka_fw
./getbuildroot.sh

```
### Build
```bash
source sourceme.first
cd buildroot
# If you want to use the build in a Docker container, then run the following command here:
#  utils/docker-run
make pluto_maiasdr_defconfig && make
```

For a list all supported boards run (this might take a while):
```bash
make list-defconfigs
```
The items at the bottom are the ones supported by Tezuka.

### Building on WSL2
Buildroot does not allow whitespaces in the PATH environment variable. On WSL several paths with whitespaces are added. The following script can be used to remove any path with whitespaces. It also deletes any leftover ':' at the end:
```bash
export PATH=$(echo $PATH | tr ':' '\n' | grep -v ' ' | tr '\n' ':' | sed 's/:$//')
```
### Compatibility with older build scripts

If you encounter errors related to CMAKE policy version, it's because newer versions of CMAKE (3.27+) have stricter policy requirements. Setting CMAKE_POLICY_VERSION_MINIMUM=3.5 tells CMAKE to use policies from version 3.5 or newer, which helps maintain compatibility with older build scripts and dependencies that may not be fully compatible with the latest CMAKE policies. This is particularly important when building packages that haven't been updated to support newer CMAKE versions.

Run the build with:

```bash
CMAKE_POLICY_VERSION_MINIMUM=3.5 make
```

### Result
All materials are in buildroot/output/images

# Credits
- Daniel Estévez for incredible maia-sdr project (https://maia-sdr.org/)
- Gwenhael Goavec-Merou for inspiration https://github.com/oscimp/PlutoSDR
- LamaBleu for helping me with buildroot and introduce me Plutosdr
- https://github.com/hz12opensource/libresdr for overclock and fpga inspiration
- All the opensource community !
