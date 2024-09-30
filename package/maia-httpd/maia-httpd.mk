################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_HTTPD_VERSION = 0.8.1
MAIA_HTTPD_SOURCE = maia-sdr-$(MAIA_HTTPD_VERSION).tar.gz
MAIA_HTTPD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_HTTPD_VERSION)
MAIA_HTTPD_SUBDIR = maia-httpd
MAIA_HTTPD_DEPENDENCIES = host-pkgconf openssl 
#CARGO_BUILD_TARGET="armv7-unknown-linux-gnueabihf"
#MAIA_HTTPD_CARGO_BUILD_OPTS += --target armv7-unknown-linux-gnueabihf 

#MAIA_HTTPD_C_CONF_ENV =  RUSTFLAGS="-Clinker=arm-linux-gnueabihf-gcc -Car=arm-linux-gnueabihf-ar"
#MAIA_HTTPD_C_MAKE_ENV = RUSTFLAGS="-Clinker=arm-linux-gnueabihf-gcc -Car=arm-linux-gnueabihf-ar"
#MAIA_HTTPD_C_CONF_OPTS = -DZENOHC_CUSTOM_TARGET="arm-unknown-linux-gnueabihf"

#MAIA_HTTPD_CARGO_OPTS = \
#   --$(MAIA_HTTPD_CARGO_MODE) \
# 	--target=armv7-unknown-linux-gnueabihf  \
# 	--manifest-path=$(@D)/Cargo.toml

#define MAIA_HTTPD_BUILD_CMDS
#    $(TARGET_MAKE_ENV) $(MAIA_HTTPD_CARGO_ENV) \
#            cargo build $(MAIA_HTTPD_CARGO_OPTS)
#endef
$(eval $(cargo-package))
