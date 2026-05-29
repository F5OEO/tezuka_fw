################################################################################
#
# soapysdr
#
################################################################################

SATDUMP_VERSION = master
SATDUMP_SITE = $(call github,SatDump,SatDump,$(SATDUMP_VERSION))
SATDUMP_INSTALL_STAGING = YES

SATDUMP_DEPENDENCIES = volk nng libpng tiff zstd jemalloc
SATDUMP_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_GUI=OFF \
-DPLUGINS_ALL=OFF -DPLUGIN_DVB=ON -DBUILD_ZIQ=OFF -DBUILD_OPENCL=OFF -DBUILD_OPENMP=OFF -DPLUGIN_RTLSDR_SDR_SUPPORT=OFF\
-DCMAKE_CXX_FLAGS="-march=armv7-a" -DCMAKE_C_FLAGS="-march=armv7-a" -DARCHFLAGS="armv7-a"
SATDUMP_INSTALL_STAGING = YES


$(eval $(cmake-package))

