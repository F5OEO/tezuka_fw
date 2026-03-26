################################################################################
#
# RXTOOLS
#
################################################################################


# Branch: master (pinned 2026-03-22)
RXTOOLS_VERSION = 811b21c4c8a592515279bd19f7460c6e4ff0551c
RXTOOLS_SITE = https://github.com/rxseger/rx_tools/archive
RXTOOLS_SOURCE = $(RXTOOLS_VERSION).tar.gz

RXTOOLS_INSTALL_STAGING = YES
RXTOOLS_LICENSE = GPL-2.0
RXTOOLS_LICENSE_FILES = COPYING
RXTOOLS_DEPENDENCIES = soapysdr


$(eval $(cmake-package))

