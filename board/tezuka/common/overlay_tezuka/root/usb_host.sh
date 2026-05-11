#!/bin/sh
#Force usb to be host : allow peripherals attached
echo "host" > /sys/bus/platform/devices/ci_hdrc.0/role