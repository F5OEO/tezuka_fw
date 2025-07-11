#!/bin/sh
# args from BR2_ROOTFS_POST_SCRIPT_ARGS
# $2    board name
set -e

BOARD_DIR=$(dirname ${0})

#rm -f ${TARGET_DIR}/usr/lib/libxml2.* -> USED BY IIO
#rm -f ${TARGET_DIR}/usr/lib/libasound.*
rm -f ${TARGET_DIR}/usr/lib/libstdc++.*
# fortran is USED by MAIA !!!!
rm -f ${TARGET_DIR}/usr/lib/libgfortran.*
rm -f ${TARGET_DIR}/lib/libgfortran.*
#rm -rf ${TARGET_DIR}/lib/firmware*
#rm -f ${TARGET_DIR}/usr/sbin/hostapd
#rm -f ${TARGET_DIR}/usr/sbin/wpa_supplicant
#rm -f ${TARGET_DIR}/usr/bin/gps*
#rm -f ${TARGET_DIR}/usr/bin/aplay
#rm -f ${TARGET_DIR}/usr/bin/arecord
#rm -f ${TARGET_DIR}/usr/sbin/gpsd
#rm -f ${TARGET_DIR}/usr/sbin/iw
#rm -f ${TARGET_DIR}/usr/sbin/wpa*
