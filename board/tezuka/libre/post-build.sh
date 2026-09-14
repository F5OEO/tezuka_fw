#!/bin/sh
# Ensure executable permissions even when the source checkout is on Windows.
set -eu
chmod 0755 "$1/usr/sbin/libresdr-tx-nco" "$1/etc/init.d/S95tx-nco"
