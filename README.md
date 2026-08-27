![tezuka banner](/doc/tezuka.png)

[![Tezuka](https://github.com/F5OEO/tezuka_fw/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/F5OEO/tezuka_fw/actions/workflows/main.yml)
[![GitHub Release](https://img.shields.io/github/release/F5OEO/tezuka_fw.svg)](https://github.com/F5OEO/tezuka_fw/releases/latest)  [![Github Releases](https://img.shields.io/github/downloads/F5OEO/tezuka_fw/total.svg)](https://github.com/F5OEO/tezuka_fw/releases/latest)

## About tezuka firmware 

* **Universal firmware** builder designed to unlock the full potential of your PlutoSDR/clone. 

* Built entirely for the **SDR enthusiast** community—bringing new features, wider frequency ranges, and massive performance boosts to your existing device.

* **Quick addition** of new board (already 10 boards supported)

---

## Key Features

* **Frequency Extension:** Expanded tuning range from **47.5 MHz to 6 GHz**.
* **Seamless Input Switching:** Easily switch between RX1/RX2 and TX1/TX2.
* **Extended Bandwidth:** Complex 8-bit mode unlocks stable streaming up to **14 MHz over USB** and **45 MHz over GbE network**.
* **Risk-Free Booting:** Built-in **SD Card boot support** for effortless updates with zero risk of bricking your flash memory.
* **Integrated Apps:** Includes Maia-SDR transparently, fast sweep, and MQTT status publishing and more.
* **Remote Access:** Built-in WireGuard VPN, configured from the Dashboard by pasting a `wg-quick` config — no SSH/CLI needed.
* **Signal Classifier:** Onboard PSD spectral-shape matching identifies signal types live, overlaid right on the Dashboard's spectrum view.

---

## Support the Project

This firmware is developed entirely in my free time. Maintaining multiple boards, buying hardware for testing, and adding features takes significant time and resources. If **tezuka** supercharges your SDR experience, please consider supporting the project!

[![GitHub Sponsors](https://img.shields.io/badge/Sponsor-GitHub-ea4aaa?style=for-the-badge&logo=github-sponsors)](https://github.com/sponsors/F5OEO)
[![PayPal](https://img.shields.io/badge/Donate-PayPal-00457C?style=for-the-badge&logo=paypal)](https://paypal.me/f5oeodev)

*No funds? You can still support by **starring the repository** or helping improve the documentation!*

---

## Hardware Matrix

| Device Platform | Architecture | SD card | Gbe | DATV | USB Type | GPIO | Trusted vendors |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| PlutoSDR | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | micro | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [digikey](https://www.digikey.com/) [mouser](https://mouser.com/)|
| PlutoPlus | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | micro | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [OpenSdrLab](https://opensourcesdrlab.com/products/opensourcesdr-lab-pluto-sdr-ad9363-2t2r-radio-sdr-transceiver-radio-70mhz-6ghz-software-defined-radio?DIST=RkVFGVU%3D)|
| Antsdr E200 | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [CrowdSupply](https://www.crowdsupply.com/microphase-technology/antsdr-e200)|
| Antsdr E310 | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [Microphase](https://www.microphase.cn/)|
| Fishball/PlutoSky | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [OpenSDRLab obsolete -> 7020](https://opensourcesdrlab.com/products/new-7020-ad9363-plutosdr?DIST=RkVFGVU%3D) [OpenSDRLab with box](https://opensourcesdrlab.com/products/opensourcesdrlab-new-7020-sdr-ad9363-for-pluto-software-defined-radio-without-pa?DIST=RkVFGVU%3D)|
| Fishball/PlutoSky | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [OpenSDRLab](https://opensourcesdrlab.com/products/new-7020-ad9363-plutosdr?DIST=RkVFGVU%3D) [OpenSDRLab with box](https://opensourcesdrlab.com/products/opensourcesdrlab-new-7020-sdr-ad9363-for-pluto-software-defined-radio-without-pa?DIST=RkVFGVU%3D) |
| SignalSDRPro | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-3 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [CrowdSupply](https://www.crowdsupply.com/signalens/signalsdr-pro)|
| LibreSDR/ZynqSDR | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [OpenSDRLab](https://opensourcesdrlab.com/products/libresdr-zynqsdr-ad9363-zynq7020?DIST=RkVFGVU%3D)|
| PlutoSky R2 | 7020 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | [OpenSDRLab](https://opensourcesdrlab.com/products/plutosky-r2?DIST=RkVFGVU%3D)|
| Pluto Nano | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | AliExpress HamGeek ?|
| PCIe SDR 7010 | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | Via PCIe <span><img src="https://img.shields.io/badge/-%E2%9C%93-green" alt="Yes"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | [OpenSDRLab](https://opensourcesdrlab.com/products/opensourcesdrlab-pcie-version-plutosdr-ad9363-xc7z010?DIST=RkVFGVU%3D)|
| OpenSDRLab 7010 Mini | 7010 | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | usb-c | <span><img src="https://img.shields.io/badge/-%E2%9C%97-red" alt="No"></span> | [OpenSDRLab](https://opensourcesdrlab.com/products/7010-ad9363-sdr-mini?DIST=RkVFGVU%3D)|


---

## Installation : Use the SD boot mode 

 **CRITICAL: DO NOT use the standard `frm` flashing method. To prevent bricking, always boot via SD card mode unless you know what you are doing.**

1. Go to the [Releases Section](https://github.com/F5OEO/tezuka_fw/releases).
2. Download and unzip the specific package matching your hardware configuration.
3. Format a fresh SD Card to **FAT32**.
4. Copy the entire contents of the **`sdimg`** folder onto the root of the SD Card.
5. Insert into your board and power on.

***Only for original PlutoSDR (No SD Slot):*** Follow the [official ADI flashing procedure](https://wiki.analog.com/university/tools/pluto/users/firmware) using the `pluto.frm` file.

---

## Configuration & Software

Once booted, the firmware exposes a USB drive containing `config.txt`. Modify this file to tweak your settings (based on standard [ADI customization parameters](https://wiki.analog.com/university/tools/pluto/users/customizing)).

### Compatible Software
Enjoy extra features out-of-the-box with custom-tailored software branches:
* [SatDump](https://github.com/F5OEO/SatDump)
* [SDR++](https://github.com/F5OEO/SDRPlusPlus)
* [SoapyPlutoPAPR](https://github.com/F5OEO/SoapyPlutoPAPR)
*(Standard apps like SDRConsole and SDRAngel are also compatible).*

### Remote Access (WireGuard VPN)

Connect your board to a remote WireGuard server for secure remote access, configured entirely from the Dashboard — no SSH/CLI needed:

* Open the Dashboard's **Network** tab and find the **VPN (WireGuard)** card.
* Paste a complete `wg-quick`-style config (`[Interface]`/`[Peer]` blocks, private key included) into the text box and click **Save & apply**.
* Toggle **Enabled** to bring the tunnel up. Live status (handshake time, transfer, peer public key) is shown on the same card.
* Routing follows whatever `AllowedIPs` your config specifies — a scoped value gives split-tunnel access to just that network, while `0.0.0.0/0` routes all device traffic through the tunnel.

**Security note:** the pasted config is sent once and never redisplayed or echoed back over MQTT. That said, the Dashboard's default MQTT channel (port 9001) is unauthenticated on the local network by design, same as every other Dashboard control — treat the WireGuard private key like any other credential on an open network segment, and avoid exposing the device's management interface to untrusted networks.

### Signal Classifier

Onboard PSD spectral-shape matching — identifies signal *types* (OFDM, FSK, chirp, CW, etc.) live and overlays a label on the spectrum, right on the device, no host-side processing required:

* Open the Dashboard's **Signal Classifier** page (under RF) and toggle **Enabled** — it runs as a background service, correlating the live spectrum against a set of loaded templates roughly once a second.
* A matched signal gets a label chip (e.g. "OFDM · 92%") overlaid on its own live spectrum view, plus a reference-match panel showing the live shape against the matched template, and a scrolling log of recent classifications.
* Ships with a small starter set out of the box (FM broadcast, DAB+, CW/beacon — synthesized canonical shapes, seeded on first boot) so there's something to test against immediately. Templates are built offline (see `tools/classifier_templates/`, including a script to bootstrap templates directly from a live device — no GNU Radio/TorchSig install required for a first test) and pushed to the device the same way a WireGuard config is: base64 over MQTT (`cmd/classifier/templates`), which replaces the starter set permanently.
* Below the confidence threshold, or with no templates loaded, the classifier reports "unknown" rather than forcing a guess.

This is spectral-*shape* matching (magnitude/PSD correlation), not true cyclostationary SCF — it separates signal types well but won't reliably distinguish modulation order within a family (e.g. BPSK vs. QPSK). It reuses the same live FFT feed the Spectrum and Radio Astronomy pages already consume (`maia-httpd`'s `/waterfall`), so it adds no new consumer of the ADC and never competes with IQ Tape or the Signal Generator.

The same page also shows a **band plan** — a color-coded strip under the spectrum marking known allocations (broadcast, cellular, aviation, maritime, etc.), independent of whether the classifier has actually matched anything there. Click a swatch (or a row in the editable table below it) to tune to that frequency. Entries are stored on the device (`/mnt/jffs2/bandplan.json`, seeded from a starting reference table) and edited directly from the Dashboard — add, edit, or remove entries and hit **Save**.

---

## For Developers

### Setup Environment (Debian/Ubuntu)
```bash
# Install mandatory dependencies
sudo apt install pkg-config libssl-dev libclang-dev jq

# Clone the repository & pull Buildroot
git clone [https://github.com/F5OEO/tezuka_fw](https://github.com/F5OEO/tezuka_fw)
cd tezuka_fw
./getbuildroot.sh


```
### Build
```bash
# Build a single board (each board gets its own output directory):
./build.sh fishball

# Build multiple boards:
./build.sh pluto plutoplus fishball

# Build all boards:
./build.sh all

# Build with parallel jobs and clean output first:
./build.sh -j8 -c fishball
```

Or build manually using Buildroot directly:
```bash
source sourceme.first
cd buildroot
make pluto_maiasdr_defconfig && make
```

For a list all supported boards run:
```bash
./build.sh -h
```

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
