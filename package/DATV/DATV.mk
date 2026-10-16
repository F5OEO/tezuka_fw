################################################################################
#
# DATV
#
################################################################################

# Installs the DATV rootfs overlay (init script S97datv, Webtioune's mpegts.js,
# ...) when the DATV package is selected, instead of listing it in each
# defconfig's BR2_ROOTFS_OVERLAY.
DATV_VERSION = local
DATV_SITE_METHOD = local
DATV_SITE = $(BR2_EXTERNAL_PLUTOSDR_PATH)/board/tezuka/common/overlay_datv
# longmynd also installs an S97datv: ours has to land after it and replace it.
DATV_DEPENDENCIES = longmynd zynqrx

# Webtioune's player: the latest mpegts.js from its GitHub main branch, built (webpack production, minified)
# on the host at build time. Needs git, node, npm and network access, like the Dashboard build. If that
# fails, the build goes on with the copy that is vendored in the overlay (root/vendor/mpegts/mpegts.js).
# DATV_MPEGTS_BRANCH can be set to another branch or tag. The result is built again with DATV-rebuild.
DATV_MPEGTS_REPO = https://github.com/xqq/mpegts.js.git
DATV_MPEGTS_BRANCH ?= master

define DATV_BUILD_CMDS
	$(DATV_PKGDIR)/build-mpegts.sh $(@D)/mpegts-build $(@D)/mpegts-out \
		$(DATV_MPEGTS_REPO) $(DATV_MPEGTS_BRANCH) \
		|| { echo "DATV: mpegts.js could not be built, the vendored copy is used"; rm -rf $(@D)/mpegts-out; }
endef

define DATV_INSTALL_TARGET_CMDS
	# $(@D) also holds Buildroot's own .stamp_* files: copy from the source tree
	cp -a $(DATV_SITE)/. $(TARGET_DIR)/
	if [ -s $(@D)/mpegts-out/mpegts.js ]; then \
		$(INSTALL) -D -m 0644 $(@D)/mpegts-out/mpegts.js $(TARGET_DIR)/root/vendor/mpegts/mpegts.js; \
		$(INSTALL) -D -m 0644 $(@D)/mpegts-out/VERSION $(TARGET_DIR)/root/vendor/mpegts/VERSION; \
	fi
endef

$(eval $(generic-package))
