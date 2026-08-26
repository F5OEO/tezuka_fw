################################################################################
#
# CLASSIFIER
#
################################################################################


CLASSIFIER_VERSION = master
CLASSIFIER_SITE_METHOD = local
CLASSIFIER_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/classifier
CLASSIFIER_DEPENDENCIES = libwebsockets

define CLASSIFIER_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define CLASSIFIER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/classifier $(TARGET_DIR)/usr/bin/classifier
endef

$(eval $(generic-package))
