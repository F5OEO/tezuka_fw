INSTALL=install

rm -Rf ${TARGET_DIR}/etc/dropbear

mkdir -p ${TARGET_DIR}/www/img
mkdir -p ${TARGET_DIR}/etc/wpa_supplicant/
mkdir -p ${TARGET_DIR}/mnt/jffs2
mkdir -p ${TARGET_DIR}/mnt/msd
mkdir -p ${TARGET_DIR}/etc/dropbear


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
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/VERSIONS ${TARGET_DIR}/opt/
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

${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/msd/img/* ${TARGET_DIR}/www/img/
${INSTALL} -D -m 0644 ${BOARD_ROOTFS}/msd/*.html ${TARGET_DIR}/www/

${INSTALL} -D -m 0755 ${BOARD_ROOTFS}/wpa_supplicant/* ${TARGET_DIR}/etc/wpa_supplicant/

ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-up.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-down.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-pre-up.d/wpasupplicant
ln -sf ../../wpa_supplicant/ifupdown.sh ${TARGET_DIR}/etc/network/if-post-down.d/wpasupplicant

ln -sf device_reboot ${TARGET_DIR}/usr/sbin/pluto_reboot
