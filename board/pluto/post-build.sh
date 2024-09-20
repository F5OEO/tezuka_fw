#!/bin/sh
# args from BR2_ROOTFS_POST_SCRIPT_ARGS
# $2    board name
set -e

BOARD_DIR=$(dirname ${0})

# Add a console on tty1
grep -qE '^ttyGS0::' ${TARGET_DIR}/etc/inittab || \
sed -i '/GENERIC_SERIAL/a\
ttyGS0::respawn:/sbin/getty -L ttyGS0 0 vt100 # USB console' ${TARGET_DIR}/etc/inittab

grep -qE '^::sysinit:/bin/mount -t debugfs' ${TARGET_DIR}/etc/inittab || \
sed -i '/hostname/a\
::sysinit:/bin/mount -t debugfs none /sys/kernel/debug/' ${TARGET_DIR}/etc/inittab

sed -i -e '/::sysinit:\/bin\/hostname -F \/etc\/hostname/d' ${TARGET_DIR}/etc/inittab

# Prepare LICENSE.html
if [ ! -e ${BINARIES_DIR}/msd ]; then
	mkdir ${BINARIES_DIR}/msd
fi

cp ${BOARD_DIR}/LICENSE.template ${BINARIES_DIR}/msd/LICENSE.html
cp ${BOARD_DIR}/msd/index.html ${BINARIES_DIR}/msd/
LINUX_VERS=$(cat ${BR2_CONFIG} | grep '^BR2_LINUX_KERNEL_VERSION' | cut -d\" -f 2)
UBOOT_VERS=$(cat ${BR2_CONFIG} | grep '^BR2_TARGET_UBOOT_VERSION' | cut -d\" -f 2)
#FW_VERSION=$(cd ${BOARD_DIR} && git describe --abbrev=4 --dirty --always --tags)
sed -i s/##DEVICE_FW##/${FW_VERSION}/g ${BINARIES_DIR}/msd/LICENSE.html
sed -i s/##LINUX_VERSION##/${LINUX_VERS}/g ${BINARIES_DIR}/msd/LICENSE.html
sed -i s/##UBOOT_VERSION##/${UBOOT_VERS}/g ${BINARIES_DIR}/msd/LICENSE.html


BOARD_DIR="$(dirname $0)"
BOARD_NAME="$(basename ${BOARD_DIR})"
GENIMAGE_CFG="${BOARD_DIR}/genimage-msd.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

rm -rf "${GENIMAGE_TMP}"

genimage                           \
	--rootpath "${TARGET_DIR}"     \
	--tmppath "${GENIMAGE_TMP}"    \
	--inputpath "${BINARIES_DIR}/msd"  \
	--outputpath "${TARGET_DIR}/opt/" \
	--config "${GENIMAGE_CFG}"

rm -f ${TARGET_DIR}/opt/boot.vfat
rm -f ${TARGET_DIR}/etc/init.d/S99iiod
rm -rf ${BINARIES_DIR}/msd

########################## ROOT FS INSTALL #############################

INSTALL=install

BOARD_ROOTFS="${BOARD_DIR}/rootfs"
rm -Rf ${TARGET_DIR}/etc/dropbear

mkdir -p ${TARGET_DIR}/www/img
mkdir -p ${TARGET_DIR}/etc/wpa_supplicant/
mkdir -p ${TARGET_DIR}/mnt/jffs2
mkdir -p ${TARGET_DIR}/mnt/msd
mkdir -p ${TARGET_DIR}/etc/dropbear

#${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/update.sh ${TARGET_DIR}/sbin/
#${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/update_from_github.sh ${TARGET_DIR}/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/update_frm.sh ${TARGET_DIR}/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/udc_handle_suspend.sh ${TARGET_DIR}/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S10mdev ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S15watchdog ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S20urandom ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S21misc ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S23udc ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S40network ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S41network ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S45msd ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/S98autostart ${TARGET_DIR}/etc/init.d/
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/fw_env.config ${TARGET_DIR}/etc/
#${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/VERSIONS ${TARGET_DIR}/opt/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/device_reboot ${TARGET_DIR}/usr/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/device_passwd ${TARGET_DIR}/usr/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/device_persistent_keys ${TARGET_DIR}/usr/sbin/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/device_format_jffs2 ${TARGET_DIR}/usr/sbin/
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/motd ${TARGET_DIR}/etc/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/test_ensm_pinctrl.sh ${TARGET_DIR}/usr/sbin/
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/device_config ${TARGET_DIR}/etc/
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/mdev.conf ${TARGET_DIR}/etc/
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/automounter.sh ${TARGET_DIR}/lib/mdev/automounter.sh
${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/ifupdown.sh ${TARGET_DIR}/lib/mdev/ifupdown.sh
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/input-event-daemon.conf ${TARGET_DIR}/etc/

${INSTALL} -D -m 0644 ${BOARD_DIR}/msd/img/* ${TARGET_DIR}/www/img/
${INSTALL} -D -m 0644 ${BOARD_DIR}/msd/*.html ${TARGET_DIR}/www/

${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/wpa_supplicant/* ${TARGET_DIR}/etc/wpa_supplicant/

ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-up.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-down.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-pre-up.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-post-down.d/wpasupplicant

ln -sf device_reboot ${TARGET_DIR}/usr/sbin/pluto_reboot