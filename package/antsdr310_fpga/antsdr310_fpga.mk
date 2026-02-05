################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

ANTSDR310_FPGA_VERSION = v0.38
ANTSDR310_FPGA_SOURCE =system_top.xsa
ANTSDR310_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/e310/bitstream/maia-iio
ANTSDR310_FPGA_SITE_METHOD = local
ANTSDR310_FPGA_INSTALL_IMAGES = YES
ANTSDR310_FPGA_INSTALL_TARGET = NO

define ANTSDR310_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(ANTSDR310_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
