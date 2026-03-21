################################################################################
#
# maia-httpd
#
################################################################################

# Branch: main (pinned 2026-03-21)
# Must match MAIA_WASM_VERSION — both live in the same monorepo
MAIA_HTTPD_VERSION = 2637b59891bdfe38d8c5bf52c84984b3da68064e
MAIA_HTTPD_SITE = https://github.com/F5OEO/maia-sdr.git
MAIA_HTTPD_SITE_METHOD = git
MAIA_HTTPD_DEPENDENCIES = host-openssl

MAIA_HTTPD_CROSS_COMPILE = arm-none-linux-gnueabihf-
MAIA_HTTPD_TOOLCHAIN_GCC = $(HOST_DIR)/bin/$(MAIA_HTTPD_CROSS_COMPILE)gcc
MAIA_HTTPD_TOOLCHAIN_GFORTRAN = $(HOST_DIR)/bin/$(MAIA_HTTPD_CROSS_COMPILE)gfortran

define MAIA_HTTPD_BUILD_CMDS
	cd $(@D)/maia-httpd && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	OPENSSL_DIR="$(HOST_DIR)" \
	OPENBLAS_TARGET=armv7 \
	OPENBLAS_HOSTCC=gcc \
	BINDGEN_EXTRA_CLANG_ARGS_armv7_unknown_linux_gnueabihf="--sysroot=$(HOST_DIR)/arm-buildroot-linux-gnueabihf/sysroot" \
	OPENBLAS_CC="$(MAIA_HTTPD_TOOLCHAIN_GCC)" \
	OPENBLAS_FC="$(MAIA_HTTPD_TOOLCHAIN_GFORTRAN)" \
	cargo build --release --target armv7-unknown-linux-gnueabihf \
		--config 'target.armv7-unknown-linux-gnueabihf.linker="$(MAIA_HTTPD_TOOLCHAIN_GCC)"'
endef

define MAIA_HTTPD_INSTALL_TARGET_CMDS
	xz -f -k $(@D)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd
	$(INSTALL) -D \
		$(@D)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd.xz \
		$(TARGET_DIR)/usr/bin/
endef

$(eval $(generic-package))
