################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

SIGNALSDRPRO_FPGA_VERSION = v0.38
SIGNALSDRPRO_FPGA_SOURCE =system_top.xsa
SIGNALSDRPRO_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/signalsdrpro/bitstream/maia-iio
SIGNALSDRPRO_FPGA_SITE_METHOD = local
SIGNALSDRPRO_FPGA_INSTALL_IMAGES = YES
SIGNALSDRPRO_FPGA_INSTALL_TARGET = NO

define SIGNALSDRPRO_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(SIGNALSDRPRO_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
