################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_HTTPD_VERSION = 0.9.0
MAIA_HTTPD_SOURCE = maia-sdr-$(MAIA_HTTPD_VERSION).tar.gz
MAIA_HTTPD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_HTTPD_VERSION)
MAIA_HTTPD_DEPENDENCIES = host-openssl
MAIA_HTTPD_CONF_OPTS += -DWITH_SSL=system

CROSS_COMPILE = arm-none-linux-gnueabihf-
TOOLCHAIN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gcc
TOOLCHAINS = "$(HOST_DIR)/bin/$(CROSS_COMPILE)gcc"
TOOLCHAIN_FORTRAN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gfortran



define MAIA_HTTPD_BUILD_CMDS
$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR)/maia-httpd && \
OPENSSL_DIR=$(HOST_DIR) OPENBLAS_TARGET=armv7 OPENBLAS_HOSTCC=gcc \
	  OPENBLAS_CC=$(TOOLCHAIN) OPENBLAS_FC=$(TOOLCHAIN_FORTRAN) cargo build --release --target armv7-unknown-linux-gnueabihf \
	  --config target.armv7-unknown-linux-gnueabihf.linker='"' $(TOOLCHAINS)'"' ")

endef 

define MAIA_HTTPD_INSTALL_TARGET_CMDS
    $(INSTALL) -D \
            $(MAIA_HTTPD_SRCDIR)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd \
            $(TARGET_DIR)/usr/bin/maia-httpd

endef

$(eval $(generic-package))
