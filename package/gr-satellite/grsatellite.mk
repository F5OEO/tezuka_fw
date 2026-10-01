################################################################################
#
# PACKAGE_GR_SATELLITE
#
################################################################################


GR_SATELLITE_VERSION = a1d8e93d2aa7f71b121c7e8d7d3abd19a1ac61f4 # main, pinned 2026-08-29

GR_SATELLITE_SITE = $(call github,daniestevez,gr-satellites,$(GR_SATELLITE_VERSION))
GR_SATELLITE_STAGING = YES

GR_SATELLITE_CONF_ENV += $(PKG_PYTHON_SETUPTOOLS_ENV)
$(eval $(cmake-package))

