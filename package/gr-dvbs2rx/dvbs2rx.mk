################################################################################
#
# PACKAGE_GR_DVBS2RX
#
################################################################################


GR_DVBS2RX_VERSION = master

GR_DVBS2RX_SITE = $(call github,igorauad,gr-dvbs2rx,$(GR_DVBS2RX_VERSION))
#GR_DVBS2RX_SITE=https://github.com/igorauad/gr-dvbs2rx.git
GR_DVBS2RX_STAGING = YES
#GR_DVBS2RX_SITE_METHOD = git
#GR_DVBS2RX_GIT_SUBMODULES = YES
GR_DVBS2RX_CONF_OPTS = -DPROCESSOR_IS_ARM=TRUE 


$(eval $(cmake-package))

