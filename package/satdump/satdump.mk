################################################################################
#
# soapysdr
#
################################################################################

SATDUMP_VERSION = master
SATDUMP_SITE = $(call github,SatDump,SatDump,$(SATDUMP_VERSION))
SATDUMP_INSTALL_STAGING = YES

SATDUMP_DEPENDENCIES = volk nng libpng tiff zstd jemalloc

# Fix: Surgically remove the exact lines forcing -march=native
define SATDUMP_REMOVE_MARCH_NATIVE
	$(SED) '/-march=native/d' $(@D)/CMakeLists.txt
endef

SATDUMP_POST_PATCH_HOOKS += SATDUMP_REMOVE_MARCH_NATIVE

SATDUMP_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_GUI=OFF \
-DPLUGINS_ALL=OFF -DPLUGIN_DVB=ON -DBUILD_ZIQ=OFF -DBUILD_OPENCL=OFF -DBUILD_OPENMP=OFF -DPLUGIN_RTLSDR_SDR_SUPPORT=OFF \
-DANGELSCRIPT_GENERIC_ONLY=ON -DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -DANGELSCRIPT_GENERIC_ONLY"

SATDUMP_INSTALL_STAGING = YES


$(eval $(cmake-package))

