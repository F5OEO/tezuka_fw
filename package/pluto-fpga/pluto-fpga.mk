################################################################################
#
# FPGA pluto-system-top-bit
#
################################################################################

PLUTO_FPGA_VERSION = 1978df2985ce230f3a50b717accd7066609866ec
#PLUTO_FPGA_SOURCE = https://github.com/analogdevicesinc/hdl.git
PLUTO_FPGA_SITE = https://github.com/analogdevicesinc/hdl.git
PLUTO_FPGA_SITE_METHOD = git

PLUTO_FPGA_INSTALL_IMAGES = YES
PLUTO_FPGA_INSTALL_STAGING = NO
PLUTO_FPGA_INSTALL_TARGET = NO

VIVADO_VERSION ?= 2022.2
VIVADO_SETTINGS ?= /opt/Xilinx/Vivado/$(VIVADO_VERSION)/settings64.sh
HAVE_VIVADO= $(shell bash -c "source $(VIVADO_SETTINGS) > /dev/null 2>&1 && vivado -version > /dev/null 2>&1 && echo 1 || echo 0")

define PLUTO_FPGA_BUILD_CMDS
	$(shell bash -c "source $(VIVADO_SETTINGS) && make -s -C $(@D)/projects/pluto 1> /dev/null")
endef

define PLUTO_FPGA_INSTALL_IMAGES_CMDS
	$(UNZIP) -o $(@D)/projects/pluto/pluto.sdk/system_top.xsa system_top.bit -d $(@D)
	cp $(@D)/system_top.bit $(BINARIES_DIR)
endef



$(eval $(generic-package))
