################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

ANTSDR_FPGA_VERSION = v0.38
ANTSDR_FPGA_SOURCE =system_top.xsa
ANTSDR_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/e200/bitstream/maia-iio
ANTSDR_FPGA_SITE_METHOD = local
ANTSDR_FPGA_INSTALL_IMAGES = YES
ANTSDR_FPGA_INSTALL_TARGET = NO

define ANTSDR_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(ANTSDR_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
