################################################################################
#
# CPDETECT
#
################################################################################


CPDETECT_VERSION = master
CPDETECT_SITE_METHOD = local
CPDETECT_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/cpdetect
CPDETECT_DEPENDENCIES = libiio

define CPDETECT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CPDETECT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/cpdetect $(TARGET_DIR)/usr/bin/cpdetect
endef

$(eval $(generic-package))
