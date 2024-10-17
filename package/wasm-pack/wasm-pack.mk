################################################################################
#
# WASM_PACK
#
################################################################################

WASM_PACK_VERSION = v0.13.0
WASM_PACK_SITE = https://github.com/rustwasm/wasm-pack.git
WASM_PACK_SITE_METHOD = git


define WASM_PACK_BUILD_CMDS
$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(WASM_PACK_SRCDIR) && \
OPENSSL_DIR=$(HOST_DIR) cargo build --release ")


endef 

define WASM_PACK_INSTALL_TARGET_CMDS
    $(INSTALL) -D \
            $(WASM_PACK_SRCDIR)/target/release/wasm-pack \
            $(HOST_DIR)/bin/

endef

$(eval $(generic-package))
