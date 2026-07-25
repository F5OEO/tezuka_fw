################################################################################
#
# MQTT_PUBD
#
################################################################################


MQTT_PUBD_VERSION = master
MQTT_PUBD_SITE_METHOD = local
MQTT_PUBD_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/mqtt_pubd
MQTT_PUBD_DEPENDENCIES = mosquitto

define MQTT_PUBD_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define MQTT_PUBD_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mqtt_pubd $(TARGET_DIR)/usr/bin/mqtt_pubd
endef

$(eval $(generic-package))
