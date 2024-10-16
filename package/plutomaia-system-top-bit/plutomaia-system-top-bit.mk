################################################################################
#
# pluto-system-top-bit
#
################################################################################

PLUTOMAIA_SYSTEM_TOP_BIT_VERSION = v0.7.0
PLUTOMAIA_SYSTEM_TOP_BIT_SOURCE = plutosdr-jtag-bootstrap-maia-sdr-$(PLUTOMAIA_SYSTEM_TOP_BIT_VERSION).zip
PLUTOMAIA_SYSTEM_TOP_BIT_SITE = https://github.com/maia-sdr/plutosdr-fw/releases/download/maia-sdr-$(PLUTOMAIA_SYSTEM_TOP_BIT_VERSION)

PLUTOMAIA_SYSTEM_TOP_BIT_INSTALL_IMAGES = YES
PLUTOMAIA_SYSTEM_TOP_BIT_INSTALL_TARGET = NO

define PLUTOMAIA_SYSTEM_TOP_BIT_EXTRACT_CMDS
	$(UNZIP) $(PLUTOMAIA_SYSTEM_TOP_BIT_DL_DIR)/$(PLUTOMAIA_SYSTEM_TOP_BIT_SOURCE) system_top.bit -d $(@D)
endef

define PLUTOMAIA_SYSTEM_TOP_BIT_INSTALL_IMAGES_CMDS
	cp $(@D)/system_top.bit $(BINARIES_DIR)
	#cp $(BOARD_DIR)/bitstream/fsbl.elf $(BINARIES_DIR)
endef


$(eval $(generic-package))

#https://github.com/maia-sdr/plutosdr-fw/releases/download/v0.7.0/plutosdr-jtag-bootstrap-maia-sdr-v0.7.0.zip
#https://github.com/maia-sdr/plutosdr-fw/releases/download/maia-sdr-v0.7.0/plutosdr-jtag-bootstrap-maia-sdr-v0.7.0.zip