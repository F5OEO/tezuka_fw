################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_HTTPD_VERSION = 0.8.1
MAIA_HTTPD_SOURCE = maia-sdr-$(MAIA_HTTPD_VERSION).tar.gz
MAIA_HTTPD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_HTTPD_VERSION)
#MAIA_HTTPD_SUBDIR = maia-httpd
#HOST_MAIA_HTTPD_DEPENDENCIES = host-pkgconf host-openssl host-rustc 

CROSS_COMPILE = arm-none-linux-gnueabihf-
TOOLCHAIN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gcc
TOOLCHAINS = "$(HOST_DIR)/bin/$(CROSS_COMPILE)gcc"
TOOLCHAIN_FORTRAN = $(HOST_DIR)/bin/$(CROSS_COMPILE)gfortran




#MAIA_HTTPD_CARGO_ENV = OPENBLAS_TARGET=armv7 OPENBLAS_HOSTCC=gcc OPENBLAS_CC=$(TOOLCHAIN) OPENBLAS_FC=$(TOOLCHAIN_FORTRAN) 
#CARGO_BUILD_TARGET="armv7-unknown-linux-gnueabihf"
#MAIA_HTTPD_CARGO_OPTS = --release --target armv7-unknown-linux-gnueabihf 

#MAIA_HTTPD_C_CONF_ENV =  RUSTFLAGS="-Clinker=arm-linux-gnueabihf-gcc -Car=arm-linux-gnueabihf-ar"
#MAIA_HTTPD_C_MAKE_ENV = RUSTFLAGS="-Clinker=arm-linux-gnueabihf-gcc -Car=arm-linux-gnueabihf-ar"
#MAIA_HTTPD_C_CONF_OPTS = -DZENOHC_CUSTOM_TARGET="arm-unknown-linux-gnueabihf"

#MAIA_HTTPD_HOST_CARGO_OPTS = \
#   --$(MAIA_HTTPD_CARGO_MODE) \
# 	--target=armv7-unknown-linux-gnueabihf  \
# 	--manifest-path=$(@D)/Cargo.toml

#MAIA_HTTPD_CARGO_ENV = \
#    CARGO_HOME=$(HOST_DIR)/usr/share/cargo \
#    RUST_TARGET_PATH=$(HOST_DIR)/bin/rustc

MAIA_HTTPD_CARGO_OPTS = \
#    --target=$(GNU_TARGET_NAME) \
#    --manifest-path=$(@D)/Cargo.toml



#define MAIA_HTTPD_BUILD_CMDS
#	cd $(MAIA_HTTPD_SRCDIR) && \
#    $(TARGET_MAKE_ENV) $(MAIA_HTTPD_CARGO_ENV) \
#            cargo build $(MAIA_HTTPD_CARGO_OPTS)
#endef

#$(shell bash -c "source ~/.cargo/env && cd $(MAIA_HTTPD_SRCDIR) && cargo build --target armv7-unknown-linux-gnueabihf")

#$(shell bash -c "PATH="$(HOME)/.cargo/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR) && \
#$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR) && \

define MAIA_HTTPD_BUILD_CMDS
$(shell bash -c "PATH="$(HOME)/.cargo/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR)/maia-httpd && \
OPENBLAS_TARGET=armv7 OPENBLAS_HOSTCC=gcc \
	  OPENBLAS_CC=$(TOOLCHAIN) OPENBLAS_FC=$(TOOLCHAIN_FORTRAN) cargo build --release --target armv7-unknown-linux-gnueabihf \
	  --config target.armv7-unknown-linux-gnueabihf.linker='"' $(TOOLCHAINS)'"' ")
$(shell bash -c "PATH="$(HOME)/.cargo/bin:$(PATH)" && cd $(MAIA_HTTPD_SRCDIR)/maia-wasm && wasm-pack build --target web")

endef 

define MAIA_HTTPD_INSTALL_TARGET_CMDS
    $(INSTALL) -D \
            $(MAIA_HTTPD_SRCDIR)/maia-httpd/target/armv7-unknown-linux-gnueabihf/release/maia-httpd \
            $(TARGET_DIR)/usr/bin/maia-httpd
endef

$(eval $(generic-package))
