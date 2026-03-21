################################################################################
#
# maia-wasm
#
################################################################################

# Branch: main (pinned 2026-03-21)
# Must match MAIA_HTTPD_VERSION — maia-wasm Cargo.toml has path deps into maia-httpd/
MAIA_WASM_VERSION = 2637b59891bdfe38d8c5bf52c84984b3da68064e
MAIA_WASM_SITE = https://github.com/F5OEO/maia-sdr.git
MAIA_WASM_SITE_METHOD = git
MAIA_WASM_DEPENDENCIES = rust-wasm wasm-pack

define MAIA_WASM_BUILD_CMDS
	cd $(@D)/maia-wasm && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	$(HOST_DIR)/bin/cargo generate-lockfile && \
	PATH="$(HOST_DIR)/bin:$$PATH" \
	$(HOST_DIR)/bin/wasm-pack build --target web
endef

define MAIA_WASM_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/root/pkg
	cp -r $(@D)/maia-wasm/pkg/* $(TARGET_DIR)/root/pkg/
	cp -r $(@D)/maia-wasm/assets/* $(TARGET_DIR)/root/
endef

$(eval $(generic-package))
