#!/bin/sh

case "${ACTION}" in
add|"")
	ifconfig ${MDEV} up
	ifup ${MDEV}
	echo $(ip -f inet -o addr show ${MDEV}|cut -d\  -f 7 | cut -d/ -f 1) > /opt/ipaddr-${MDEV}
	;;
remove)
	ifdown ${MDEV}
	;;
esac
