################################################################################
#
# longmynd
#
################################################################################

LONGMYND_VERSION = 2a29f0b65f38c79dd34b693a98921f118f13d731 # master, pinned 2026-10-12
LONGMYND_SITE = $(call github,F5OEO,longmynd,$(LONGMYND_VERSION))
LONGMYND_LICENSE = GPL-3.0+
LONGMYND_LICENSE_FILES = LICENSE
LONGMYND_DEPENDENCIES = civetwebws libusb mosquitto zlib

# The beeper is the only ALSA user; swap it for a stub so alsa-lib is not needed.
define LONGMYND_USE_BEEP_STUB
	cp $(LONGMYND_PKGDIR)/beep_stub.c $(@D)/beep.c
endef
LONGMYND_POST_PATCH_HOOKS += LONGMYND_USE_BEEP_STUB

# Only the longmynd binary is built: the upstream default target also runs
# git describe and zip (archive), and the WebSocket output (udp.c) needs civetweb.
define LONGMYND_BUILD_CMDS
	$(MAKE) -C $(@D) longmynd \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS) -fpermissive -Wno-unused-function -Wno-unused-variable" \
		LDFLAGS="$(TARGET_LDFLAGS) -lusb-1.0 -lm -lpthread -lmosquitto -lcivetweb -lz"
endef

define LONGMYND_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/longmynd $(TARGET_DIR)/usr/bin/longmynd
	$(INSTALL) -D -m 0755 $(LONGMYND_PKGDIR)/S97datv $(TARGET_DIR)/etc/init.d/S97datv
	$(INSTALL) -D -m 0755 $(LONGMYND_PKGDIR)/mqtt_longmynd.sh $(TARGET_DIR)/root/mqtt_longmynd.sh
endef

$(eval $(generic-package))
