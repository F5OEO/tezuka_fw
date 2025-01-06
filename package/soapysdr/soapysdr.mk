################################################################################
#
# soapysdr
#
################################################################################

SOAPYSDR_VERSION = soapy-sdr-0.8.1
SOAPYSDR_SITE = https://github.com/pothosware/SoapySDR
SOAPYSDR_SITE_METHOD = git
#SOAPYSDR_MASTER_DEPENDENCIES = python
SOAPYSDR_CONF_OPTS = -DENABLE_PYTHON3=OFF -DENABLE_PYTHON=OFF
SOAPYSDR_INSTALL_STAGING = YES
# -DCFLAGS=$(TARGET_CC) -DCXXFLAGS=$(TARGET_CXX)


$(eval $(cmake-package))

