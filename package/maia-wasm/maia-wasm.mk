################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_WASM_VERSION = 0.9.0
MAIA_WASM_SOURCE = maia-sdr-$(MAIA_WASM_VERSION).tar.gz
MAIA_WASM_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_WASM_VERSION)
MAIA_WASM_DEPENDENCIES = rust-wasm wasm-pack 
define MAIA_WASM_BUILD_CMDS

$(shell bash -c "PATH="$(HOST_DIR)/bin:$(PATH)" && cd $(MAIA_WASM_SRCDIR)/maia-wasm && \
wasm-pack build --target web")

endef 

define MAIA_WASM_INSTALL_TARGET_CMDS
    
    
    mkdir -p $(TARGET_DIR)/root/pkg
    cp -r $(MAIA_WASM_SRCDIR)/maia-wasm/pkg/* $(TARGET_DIR)/root/pkg/
    cp -r $(MAIA_WASM_SRCDIR)/maia-wasm/assets/* $(TARGET_DIR)/root/
endef

$(eval $(generic-package))
