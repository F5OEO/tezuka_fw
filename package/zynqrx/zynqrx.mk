################################################################################
#
# zynqrx
#
################################################################################

ZYNQRX_VERSION = local
ZYNQRX_SITE_METHOD = local
ZYNQRX_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/zynqrx
ZYNQRX_LICENSE = GPL-3.0+
ZYNQRX_LICENSE_FILES = LICENSE
# Real build-order dependency: the Makefile links -liio and -lmosquitto
# (IIO=1 MQTT=1 are its defaults for the board).
ZYNQRX_DEPENDENCIES = libiio mosquitto

# The local site is rsynced as-is, including any build/, build-host/ and
# build-prof/ folders from a manual build in app/zynqrx. make would reuse
# those stale objects (other flags, maybe the host's), so drop them.
define ZYNQRX_CLEAN_STALE_BUILDS
	rm -rf $(@D)/build $(@D)/build-host $(@D)/build-prof
endef
ZYNQRX_POST_RSYNC_HOOKS += ZYNQRX_CLEAN_STALE_BUILDS

# Do not pass Buildroot's CXXFLAGS: the Makefile appends them after its own
# -O3 -mcpu=cortex-a9 -mfpu=neon -funsafe-math-optimizations, and Buildroot's
# -O2 would then win. The toolchain wrapper still adds the sysroot.
define ZYNQRX_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		CXX="$(TARGET_CXX)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define ZYNQRX_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/zynqrx $(TARGET_DIR)/usr/bin/zynqrx
	$(INSTALL) -D -m 0755 $(ZYNQRX_PKGDIR)/start_zynqrx.sh $(TARGET_DIR)/usr/bin/start_zynqrx
endef

$(eval $(generic-package))
