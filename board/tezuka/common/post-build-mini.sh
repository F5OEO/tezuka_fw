#!/bin/sh
set -e

#rm -f "${TARGET_DIR}/usr/lib/libxml2.*" -> USED BY IIO
# No package in this tree calls into ALSA (no asoundlib/snd_pcm reference
# anywhere in-tree) and tezuka_tools' Config.in deliberately leaves
# BR2_PACKAGE_ALSA_UTILS unselected, so libasound here is only ever a
# transitive dependency some other enabled package links against
# optionally -- unlike libxml2 above, nothing on mini builds calls it.
rm -f "${TARGET_DIR}/usr/lib/libasound.*"
rm -f "${TARGET_DIR}/usr/lib/libstdc++."*
# Mini builds exclude Maia -- safe to remove libgfortran to save space
rm -f "${TARGET_DIR}/usr/lib/libgfortran."*
rm -f "${TARGET_DIR}/lib/libgfortran."*
#rm -f "${TARGET_DIR}/usr/sbin/hostapd"
# wpa_supplicant + iw + the WiFi firmware blobs below: both boards
# using this script (fishball_mini, fishball_mini_7020) select
# CONFIG_CFG80211/CONFIG_MAC80211 in their kernel config, but every
# actual USB WiFi radio driver (RTL8150/8152/8187/8192CU/8XXXU) is
# commented out in that same config -- confirmed via
# board/tezuka/common/kernel/zynq_pluto_linux_mini_defconfig. No WiFi
# hardware, no driver, so these userspace tools (and lib/firmware's
# contents, all Ralink/Realtek USB WiFi dongle blobs -- rt28xx/rt3071/
# rt73/rtlwifi, confirmed by listing the directory, nothing else in
# there) have nothing to drive on this board.
#
# This closes a real QSPI overflow found rebuilding these two boards
# on Buildroot 2026.05.2 (pluto.frm 873,695 bytes over the
# 14,680,064-byte budget). Measured on real local builds throughout,
# not estimated -- each step below is the actual remaining overflow
# after adding it, in order:
#   wpa_supplicant+wpa_cli+wpa_passphrase+iw only:   420,483 bytes over
#   + lib/firmware (984K, all dead WiFi blobs):       81,731 bytes over
#   + nano (196K, busybox vi remains as an editor):    9,371 bytes over
#   + bash-completion (84K, tc/devlink/dpll/wg):           955 bytes over
#   + terminfo (104K, below):                        closes it
rm -f "${TARGET_DIR}/usr/sbin/wpa_supplicant"
rm -f "${TARGET_DIR}/usr/sbin/wpa_cli"
rm -f "${TARGET_DIR}/usr/sbin/wpa_passphrase"
rm -f "${TARGET_DIR}/usr/sbin/iw"
rm -rf "${TARGET_DIR}/lib/firmware"*
# nano is a plain convenience duplicate here, not a hardware feature:
# busybox's own vi (CONFIG_VI=y, confirmed present in the built
# target) is still available as an editor, so this isn't leaving the
# board with nothing to edit config files with over SSH.
rm -f "${TARGET_DIR}/usr/bin/nano"
# bash-completion scripts (tc/devlink/dpll from iproute2, wg/wg-quick
# from wireguard-tools) and the terminfo database below are both pure
# interactive-shell convenience data -- zero effect on tc/wg/ssh/etc.
# actually working, only on tab-completion and non-default-terminal
# rendering quirks at an interactive prompt, neither of which matters
# for a headless SDR appliance normally driven over SSH/scripts/API.
rm -rf "${TARGET_DIR}/usr/share/bash-completion"
rm -rf "${TARGET_DIR}/usr/share/terminfo"
# opkg (tezuka_tools_mini's select BR2_PACKAGE_OPKG) is a runtime .ipk
# package manager -- unlike everything trimmed above, this isn't a
# like-for-like substitute (busybox vi for nano) or dead due to missing
# hardware (WiFi tools with no radio driver): it's a working feature,
# just one nothing in this tree scripts or documents using, on a board
# that's flashed as a monolithic image rather than field-updated package
# by package. Confirmed via Tobias directly (2026-09-19) rather than cut
# unilaterally. libarchive is opkg's own dependency here -- confirmed
# against the real upstream package/opkg/opkg.mk (OPKG_DEPENDENCIES =
# host-pkgconf libarchive, with every other optional dep -- libcurl,
# libgpgme, libsolv, lz4, xz, zstd -- gated off since none of those
# BR2_PACKAGE_* symbols are set here) and by grepping this whole
# BR2_EXTERNAL tree for any other package selecting it: nothing else
# does, so removing opkg's binary without also removing libarchive would
# leave that library dead weight rather than reclaiming its space.
# Needed to close fishball_mini_7020's QSPI overflow: its FPGA bitstream
# is ~1.57MB bigger than fishball_mini's own (2,725,804 vs 1,153,800
# bytes, confirmed by extracting both boards' real system_top.bit from
# their .xsa packages -- the XC7Z020 has ~2.2x the fabric of the
# XC7Z010), so it needs real headroom beyond what closed fishball_mini's
# own gap to exactly 0 bytes over budget.
rm -f "${TARGET_DIR}/usr/bin/opkg"
rm -f "${TARGET_DIR}/usr/lib/libopkg.so"*
rm -f "${TARGET_DIR}/usr/lib/libarchive.so"*
rm -rf "${TARGET_DIR}/usr/lib/opkg"
rm -rf "${TARGET_DIR}/etc/opkg"
#rm -f "${TARGET_DIR}/usr/bin/gps"*
#rm -f "${TARGET_DIR}/usr/bin/aplay"
#rm -f "${TARGET_DIR}/usr/bin/arecord"
#rm -f "${TARGET_DIR}/usr/sbin/gpsd"
#rm -f "${TARGET_DIR}/usr/sbin/wpa"*
