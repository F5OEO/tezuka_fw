################################################################################
#
# libad9361-iio
#
################################################################################

# Tag v0.4.0, pinned by commit per this tree's reproducibility convention
LIBAD9361_IIO_VERSION = d9c7b1463a6c9c066bb5f7be6c3ec6e411351636 # v0.4.0
LIBAD9361_IIO_SITE = $(call github,analogdevicesinc,libad9361-iio,$(LIBAD9361_IIO_VERSION))
LIBAD9361_IIO_LICENSE = LGPL-2.1
LIBAD9361_IIO_LICENSE_FILES = COPYING.txt
LIBAD9361_IIO_INSTALL_STAGING = YES
LIBAD9361_IIO_DEPENDENCIES = libiio

LIBAD9361_IIO_CONF_OPTS = \
	-DENABLE_PACKAGING=OFF \
	-DBUILD_TESTS=OFF \
	-DWITH_DOC=OFF \
	-DPYTHON_BINDINGS=OFF \
	-DMATLAB_BINDINGS=OFF

$(eval $(cmake-package))
