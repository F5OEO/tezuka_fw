################################################################################
#
# SoapyPlutoSDR
#
################################################################################
SOAPYPLUTOSDR_VERSION = soapyplutosdr
SOAPYPLUTOSDR_SOURCE_BASENAME = SoapyPlutoPAPR-master
SOAPYPLUTOSDR_SOURCE =$(SOAPYPLUTOSDR_SOURCE_BASENAME).zip
SOAPYPLUTOSDR_SITE = $(call github,F5OEO,SoapyPlutoPAPR,master)
SOAPYPLUTOSDR_INSTALL_STAGING = YES
SOAPYPLUTOSDR_LICENSE = Boost Software License 1.0
SOAPYPLUTOSDR_LICENSE_FILES = LICENSE_1_0.txt
SOAPYPLUTOSDR_DEPENDENCIES = soapysdr
#SOAPYPLUTOSDR_MASTER_CONF_OPTS = -DENABLE_PYTHON3=OFF -DENABLE_PYTHON=OFF
# -DCFLAGS=$(TARGET_CC) -DCXXFLAGS=$(TARGET_CXX)

define SOAPYPLUTOSDR_EXTRACT_CMDS
    echo $(@D)
    unzip $(DL_DIR)/$(SOAPYPLUTOSDR_VERSION)/$(SOAPYPLUTOSDR_SOURCE_BASENAME).zip -d $(@D)
    mv $(@D)/$(SOAPYPLUTOSDR_SOURCE_BASENAME)/* $(@D)
 #   rmdir $(@D)/$(SOAPYPLUTOSDR_SOURCE_BASENAME)
endef

$(eval $(cmake-package))


