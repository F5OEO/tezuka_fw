################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

PLUTOMAIA_IIO_SYSTEM_TOP_BIT_VERSION = v0.38
PLUTOMAIA_IIO_SYSTEM_TOP_BIT_SOURCE = system_top.xsa
PLUTOMAIA_IIO_SYSTEM_TOP_BIT_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/pluto/bitstream/maia-iio
PLUTOMAIA_IIO_SYSTEM_TOP_BIT_SITE_METHOD = local
PLUTOMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
PLUTOMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define PLUTOMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(PLUTOMAIA_IIO_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
