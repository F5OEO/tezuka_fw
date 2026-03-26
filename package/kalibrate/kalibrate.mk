################################################################################
#
# KALIBRATE
#
################################################################################


# Branch: main (pinned 2026-03-22)
KALIBRATE_VERSION = 1062eeaaa8ab7c684171e3cb6b75ae52bd7e8c8c
KALIBRATE_SITE = https://github.com/F5OEO/kalibrate-hydrasdr/archive
KALIBRATE_SOURCE = $(KALIBRATE_VERSION).tar.gz

KALIBRATE_INSTALL_STAGING = YES
KALIBRATE_LICENSE = BSD-2.0
KALIBRATE_LICENSE_FILES = COPYING

$(eval $(cmake-package))

