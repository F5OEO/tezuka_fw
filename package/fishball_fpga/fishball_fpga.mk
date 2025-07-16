################################################################################
#
# fishball-system-top-bit
#
################################################################################

FISHBALL_FPGA_VERSION = v0.38
FISHBALL_FPGA_SOURCE =system_top.xsa
FISHBALL_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/fishball/bitstream/maia-iio
FISHBALL_FPGA_SITE_METHOD = local
FISHBALL_FPGA_INSTALL_IMAGES = YES
FISHBALL_FPGA_INSTALL_TARGET = NO

define FISHBALL_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(FISHBALL_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
