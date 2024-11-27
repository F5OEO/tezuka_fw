################################################################################
#
# e200-system-top-bit
#
################################################################################

E200_MAIA_IIO_SYSTEM_TOP_BIT_VERSION = v0.38
E200_MAIA_IIO_SYSTEM_TOP_BIT_SOURCE = system_top.xsa
E200_MAIA_IIO_SYSTEM_TOP_BIT_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/e200/bitstream/maia-iio
E200_MAIA_IIO_SYSTEM_TOP_BIT_SITE_METHOD = local
E200_MAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
E200_MAIA_IIO_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define E200_MAIA_IIO_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(E200_MAIA_IIO_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
