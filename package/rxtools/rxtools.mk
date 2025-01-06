################################################################################
#
# RXTOOLS
#
################################################################################


RXTOOLS_VERSION = master
RXTOOLS_SITE = https://github.com/rxseger/rx_tools.git
RXTOOLS_SITE_METHOD = git

RXTOOLS_INSTALL_STAGING = YES
RXTOOLS_LICENSE = GPL-2.0
RXTOOLS_LICENSE_FILES = COPYING
RXTOOLS_DEPENDENCIES = soapysdr


$(eval $(cmake-package))

