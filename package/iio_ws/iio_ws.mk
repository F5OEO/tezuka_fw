################################################################################
#
# IIO_WS
#
################################################################################


IIO_WS_VERSION = master
IIO_WS_SITE_METHOD = local
IIO_WS_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/iio_ws

define IIO_WS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define IIO_WS_INSTALL_TARGET_CMDS
	
	$(INSTALL) -D -m 0755 $(@D)/iio_ws_proxy $(TARGET_DIR)/usr/bin/iio_ws_proxy
endef

$(eval $(generic-package))


