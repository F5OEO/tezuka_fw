################################################################################
#
# plutoplus-system-top-bit
#
################################################################################

LIBRE_FPGA_VERSION = v0.38
LIBRE_FPGA_SOURCE =system_top.xsa
LIBRE_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/libre/bitstream/maia-iio
LIBRE_FPGA_SITE_METHOD = local
LIBRE_FPGA_INSTALL_IMAGES = YES
LIBRE_FPGA_INSTALL_TARGET = NO

define LIBRE_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(LIBRE_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
