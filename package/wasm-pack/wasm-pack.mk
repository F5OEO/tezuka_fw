################################################################################
#
# WASM_PACK
#
################################################################################

WASM_PACK_VERSION = v0.14.0
WASM_PACK_SITE = https://github.com/rustwasm/wasm-pack/archive/refs/tags
WASM_PACK_SOURCE = $(WASM_PACK_VERSION).tar.gz
WASM_PACK_DEPENDENCIES = rust-wasm

define WASM_PACK_BUILD_CMDS
	cd $(@D) && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	OPENSSL_DIR="$(HOST_DIR)" \
	cargo build --release
endef

define WASM_PACK_INSTALL_TARGET_CMDS
	$(INSTALL) -D $(@D)/target/release/wasm-pack $(HOST_DIR)/bin/
endef

$(eval $(generic-package))
