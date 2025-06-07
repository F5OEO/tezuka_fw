#!/bin/sh

# Action script to enable/disable wpa-roam interfaces in reaction to
# ifplugd events.
#
# Copyright: Copyright (c) 2008-2010, Kel Modderman <kel@otaku42.de>
# License:   GPL-2
#

PATH=/sbin:/usr/sbin:/bin:/usr/bin

if [ ! -x /sbin/wpa_action ]; then
	exit 0
fi

# ifplugd(8) - <iface> <action>
#
# If an ifplugd managed interface is brought up, disconnect any
# wpa-roam managed interfaces so that only one "roaming" interface
# remains active on the system.

IFPLUGD_IFACE="${1}"

case "${2}" in
	up)
		COMMAND=disconnect
		;;
	down)
		COMMAND=reconnect
		;;
	*)
		echo "$0: unknown arguments: ${@}" >&2
		exit 1
		;;
esac

for CTRL in /run/wpa_supplicant/*; do
	[ -S "${CTRL}" ] || continue

	IFACE="${CTRL#/run/wpa_supplicant/}"

	# skip if ifplugd is managing this interface
	if [ "${IFPLUGD_IFACE}" = "${IFACE}" ]; then
		continue
	fi

	if wpa_action "${IFACE}" check; then
		wpa_cli -i "${IFACE}" "${COMMAND}"
	fi
done
