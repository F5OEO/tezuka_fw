################################################################################
#
# soapysdr
#
################################################################################

NNG_VERSION = v1.9.0
NNG_SITE = $(call github,nanomsg,nng,$(NNG_VERSION))
NNG_INSTALL_STAGING = YES


NNG_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON 
NNG_INSTALL_STAGING = YES


$(eval $(cmake-package))

