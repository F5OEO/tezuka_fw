################################################################################
#
# board-fpga -- extract FPGA bitstream for the target board
#
################################################################################

BOARD_FPGA_VERSION = 1.0
BOARD_FPGA_SOURCE = system_top.xsa
BOARD_FPGA_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/$(call qstrip,$(BR2_PACKAGE_BOARD_FPGA_BOARD))/bitstream/maia-iio
BOARD_FPGA_SITE_METHOD = local
BOARD_FPGA_INSTALL_IMAGES = YES
BOARD_FPGA_INSTALL_TARGET = NO

define BOARD_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/$(BOARD_FPGA_SOURCE) system_top.bit -d $(@D)
	cp -f $(@D)/system_top.bit $(BINARIES_DIR)
endef

$(eval $(generic-package))
