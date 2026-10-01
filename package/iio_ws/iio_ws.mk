################################################################################
#
# IIO_WS
#
################################################################################


IIO_WS_VERSION = master
IIO_WS_SITE_METHOD = local
IIO_WS_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/app/iio_ws
# Real build-order dependency, not just a Kconfig select -- iio_ws_proxy.c
# links -liio -lwebsockets directly (see app/iio_ws/Makefile). Without
# this, nothing guarantees libiio/libwebsockets are already built and
# staged before this package's build step runs; it worked before only
# by incidental package ordering. See Config.in's comment.
IIO_WS_DEPENDENCIES = libiio libwebsockets

define IIO_WS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define IIO_WS_INSTALL_TARGET_CMDS
	
	$(INSTALL) -D -m 0755 $(@D)/iio_ws_proxy $(TARGET_DIR)/usr/bin/iio_ws_proxy
endef

$(eval $(generic-package))


