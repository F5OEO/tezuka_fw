################################################################################
#
# FutureSDR
#
################################################################################
FUTURESDR_VERSION = main
FUTURESDR_SITE = https://github.com/FutureSDR/FutureSDR.git
FUTURESDR_SITE_METHOD = git
CROSS_COMPILE = arm-none-linux-gnueabihf-
TOOLCHAIN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gcc

define FUTURESDR_BUILD_CMDS
$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(FUTURESDR_SRCDIR) && \
	  cargo build --release --target armv7-unknown-linux-gnueabihf \
	  --config target.armv7-unknown-linux-gnueabihf.linker='"' $(TOOLCHAINS)'"' ")
endef

define FUTURESDR_INSTALL_TARGET_CMDS
    $(INSTALL) -D \
            $(FUTURESDR_SRCDIR)/target/armv7-unknown-linux-gnueabihf/release/libfuturesdr.rlib \
           $(TARGET_DIR)/usr/lib/libfuturesdr.rlib
endef

$(eval $(generic-package))


