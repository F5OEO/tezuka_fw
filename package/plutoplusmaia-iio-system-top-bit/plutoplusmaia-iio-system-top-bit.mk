################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_VERSION = v0.38
PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_SOURCE = system_top.xsa
PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/plutoplus/bitstream/maia-iio
PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_SITE_METHOD = local
PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(PLUTOPLUSMAIA_IIO_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
