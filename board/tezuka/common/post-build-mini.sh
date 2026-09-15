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
#rm -rf "${TARGET_DIR}/lib/firmware"*
#rm -f "${TARGET_DIR}/usr/sbin/hostapd"
#rm -f "${TARGET_DIR}/usr/sbin/wpa_supplicant"
#rm -f "${TARGET_DIR}/usr/bin/gps"*
#rm -f "${TARGET_DIR}/usr/bin/aplay"
#rm -f "${TARGET_DIR}/usr/bin/arecord"
#rm -f "${TARGET_DIR}/usr/sbin/gpsd"
#rm -f "${TARGET_DIR}/usr/sbin/iw"
#rm -f "${TARGET_DIR}/usr/sbin/wpa"*
