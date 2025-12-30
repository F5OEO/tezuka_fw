################################################################################
#
# KALIBRATE
#
################################################################################


KALIBRATE_VERSION = main
KALIBRATE_SITE = https://github.com/F5OEO/kalibrate-hydrasdr
KALIBRATE_SITE_METHOD = git

KALIBRATE_INSTALL_STAGING = YES
KALIBRATE_LICENSE = BSD-2.0
KALIBRATE_LICENSE_FILES = COPYING

$(eval $(cmake-package))

