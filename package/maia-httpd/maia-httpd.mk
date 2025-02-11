################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_HTTPD_VERSION = sweep
MAIA_HTTPD_SITE = https://github.com/F5OEO/maia-sdr.git
MAIA_HTTPD_SITE_METHOD = git
MAIA_HTTPD_DEPENDENCIES = host-openssl
MAIA_HTTPD_CONF_OPTS += -DWITH_SSL=system

CROSS_COMPILE = arm-none-linux-gnueabihf-
TOOLCHAIN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gcc
TOOLCHAINS = "$(HOST_DIR)/bin/$(CROSS_COMPILE)gcc"
TOOLCHAIN_FORTRAN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gfortran



define MAIA_HTTPD_BUILD_CMDS
$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR)/maia-httpd && \
$(HOST_DIR)/bin/cargo generate-lockfile && \
OPENSSL_DIR=$(HOST_DIR) OPENBLAS_TARGET=armv7 OPENBLAS_HOSTCC=gcc \
BINDGEN_EXTRA_CLANG_ARGS_armv7_unknown_linux_gnueabihf="--sysroot=$(HOST_DIR)/arm-buildroot-linux-gnueabihf/sysroot" \
	  OPENBLAS_CC=$(TOOLCHAIN) OPENBLAS_FC=$(TOOLCHAIN_FORTRAN) cargo build --release --target armv7-unknown-linux-gnueabihf \
	  --config target.armv7-unknown-linux-gnueabihf.linker='"' $(TOOLCHAINS)'"' ")

endef 

define MAIA_HTTPD_INSTALL_TARGET_CMDS
    $(shell bash -c "xz -f -k $(MAIA_HTTPD_SRCDIR)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd")
    $(INSTALL) -D \
            $(MAIA_HTTPD_SRCDIR)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd.xz \
            $(TARGET_DIR)/usr/bin/

endef

$(eval $(generic-package))
