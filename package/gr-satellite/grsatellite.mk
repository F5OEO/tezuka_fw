################################################################################
#
# PACKAGE_GR_SATELLITE
#
################################################################################


GR_SATELLITE_VERSION = main

GR_SATELLITE_SITE = $(call github,daniestevez,gr-satellites,$(GR_SATELLITE_VERSION))
GR_SATELLITE_STAGING = YES

GR_SATELLITE_CONF_ENV += $(PKG_PYTHON_SETUPTOOLS_ENV)
$(eval $(cmake-package))

