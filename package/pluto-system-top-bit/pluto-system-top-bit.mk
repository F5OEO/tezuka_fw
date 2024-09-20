################################################################################
#
# pluto-system-top-bit
#
################################################################################

PLUTO_SYSTEM_TOP_BIT_VERSION = v0.38
PLUTO_SYSTEM_TOP_BIT_SOURCE = plutosdr-jtag-bootstrap-$(PLUTO_SYSTEM_TOP_BIT_VERSION).zip
PLUTO_SYSTEM_TOP_BIT_SITE = http://github.com/analogdevicesinc/plutosdr-fw/releases/download/$(PLUTO_SYSTEM_TOP_BIT_VERSION)

PLUTO_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
PLUTO_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define PLUTO_SYSTEM_TOP_BIT_EXTRACT_CMDS
	$(UNZIP) $(PLUTO_SYSTEM_TOP_BIT_DL_DIR)/$(PLUTO_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
endef

define PLUTO_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	cp $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))