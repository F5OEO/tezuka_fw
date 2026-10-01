################################################################################
#
# libgse
#
################################################################################

LIBGSE_VERSION = 0860f2a1922989a38248e7f5d3653c4df77e2477 # master, pinned 2026-08-29
LIBGSE_SITE = $(call github,F5OEO,libgse,$(LIBGSE_VERSION))
LIBGSE_INSTALL_STAGING = YES
LIBGSE_AUTORECONF = YES
LIBGSE_DEPENDENCIES +=  libpcap

$(eval $(autotools-package))