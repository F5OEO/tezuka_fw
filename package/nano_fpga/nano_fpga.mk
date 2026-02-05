################################################################################
#
# fishball-system-top-bit
#
################################################################################

NANO_FPGA_VERSION = v0.38
NANO_FPGA_SOURCE =system_top.xsa
NANO_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/nano/bitstream/maia-iio
NANO_FPGA_SITE_METHOD = local
NANO_FPGA_INSTALL_IMAGES = YES
NANO_FPGA_INSTALL_TARGET = NO

define NANO_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(NANO_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
