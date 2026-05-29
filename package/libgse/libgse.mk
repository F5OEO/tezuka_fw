################################################################################
#
# libgse
#
################################################################################

LIBGSE_VERSION = master
LIBGSE_SITE = $(call github,F5OEO,libgse,$(LIBGSE_VERSION))
LIBGSE_INSTALL_STAGING = YES
LIBGSE_AUTORECONF = YES
LIBGSE_DEPENDENCIES +=  libpcap

$(eval $(autotools-package))