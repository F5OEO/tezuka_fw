################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

PLUTOPLUS_SYSTEM_TOP_BIT_VERSION = v0.38
PLUTOPLUS_SYSTEM_TOP_BIT_SOURCE = system_top.xsa
PLUTOPLUS_SYSTEM_TOP_BIT_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/plutoplus/bitstream
PLUTOPLUS_SYSTEM_TOP_BIT_SITE_METHOD = local
PLUTOPLUS_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
PLUTOPLUS_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define PLUTOPLUS_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	$(UNZIP) $(@D)/$(PLUTOPLUS_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
	cp $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
