################################################################################
#
# libssp
#
################################################################################

#LIBSSP_VERSION = 2.2.5
LIBSSP_SITE = $(TOPDIR)/dl
LIBSSP_SOURCE = libssp.tar.bz2
#LIBSSP_INSTALL_STAGING = YES
LIBSSP_AUTORECONF = YES

$(eval $(autotools-package))
#$(eval $(generic-package))

