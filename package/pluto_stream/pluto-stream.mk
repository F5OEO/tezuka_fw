################################################################################
#
# pluto-stream
#
################################################################################

# Adjust the version / source as needed.
# If fetching from a git repo, use PLUTO_STREAM_SITE + PLUTO_STREAM_SITE_METHOD.
# If using a local tarball, use PLUTO_STREAM_SOURCE and drop the SITE lines.

PLUTO_STREAM_VERSION =tezukadvb
PLUTO_STREAM_SITE = https://github.com/F5OEO/pluto-ori-ps.git
PLUTO_STREAM_SITE_METHOD = git
PLUTO_STREAM_LICENSE = GPL-2.0+
# Upstream repo lacks a LICENSE file; omit here so legal-info doesn't hard-fail.
# License is GPL-2.0+ as noted above.
# PLUTO_STREAM_LICENSE_FILES = LICENSE

# Runtime & build dependencies
# (adjust names to match what is actually available in your Buildroot tree)
PLUTO_STREAM_DEPENDENCIES = libiio mosquitto libgse ne10

# ---- Generic-package build steps ------------------------------------------

define PLUTO_STREAM_BUILD_CMDS
	$(MAKE) -C $(@D) \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		CXXFLAGS="$(TARGET_CXXFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		all
endef

# Install directly — the upstream Makefile's install target uses an
# undefined $(PAPR_ORI) variable, so we bypass it entirely.
define PLUTO_STREAM_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pluto_mqtt_ctrl $(TARGET_DIR)/usr/bin/pluto_mqtt_ctrl
	$(INSTALL) -D -m 0755 $(@D)/pluto_stream    $(TARGET_DIR)/usr/bin/pluto_stream
	for s in qo100initdvb.sh gain.sh mute.sh unmute.sh settxmode.sh \
	         initdvb.sh mqtt_ifconfig.sh mqtt_iptable.sh mqtt_route.sh \
	         mqtt_reboot.sh mqtt_setcall.sh mqtt_longmynd.sh \
	         passthrough.sh agctest.sh watchconsoletx.sh relay.sh; do \
		if [ -f $(@D)/$$s ]; then \
			$(INSTALL) -D -m 0755 $(@D)/$$s $(TARGET_DIR)/root/dvb/$$s; \
		fi; \
	done
	$(INSTALL) -D -m 0755 $(@D)/S96plutostream    $(TARGET_DIR)/etc/init.d/
endef

$(eval $(generic-package))