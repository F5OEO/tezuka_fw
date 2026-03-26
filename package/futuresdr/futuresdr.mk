################################################################################
#
# FutureSDR
#
################################################################################
# Branch: main (pinned 2026-03-21)
FUTURESDR_VERSION = 96562fe5042c465b1a3a95a11805e6d30c1a5aeb
FUTURESDR_SITE = https://github.com/FutureSDR/FutureSDR/archive
FUTURESDR_SOURCE = $(FUTURESDR_VERSION).tar.gz

define FUTURESDR_BUILD_CMDS
	cd $(@D) && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	$(HOST_DIR)/bin/cargo generate-lockfile && \
	cd $(@D) && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	cargo build --release --target armv7-unknown-linux-gnueabihf \
		--config "target.armv7-unknown-linux-gnueabihf.linker=\"$(HOST_DIR)/bin/arm-none-linux-gnueabihf-gcc\""
endef

define FUTURESDR_INSTALL_TARGET_CMDS
	$(INSTALL) -D \
		$(@D)/target/armv7-unknown-linux-gnueabihf/release/libfuturesdr.rlib \
		$(TARGET_DIR)/usr/lib/libfuturesdr.rlib
endef

$(eval $(generic-package))


