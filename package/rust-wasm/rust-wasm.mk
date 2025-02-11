################################################################################
#
# rust-wasm
#
################################################################################

# When updating this version, check whether support/download/cargo-post-process
# still generates the same archives.
RUST_WASM_VERSION = 1.82.0
RUST_WASM_SITE = https://static.rust-lang.org/dist
RUST_WASM_LICENSE = Apache-2.0 or MIT
RUST_WASM_LICENSE_FILES = LICENSE-APACHE LICENSE-MIT

RUST_WASM_SOURCE = rust-std-$(RUST_WASM_VERSION)-wasm32-unknown-unknown.tar.gz

RUST_WASM_LIBSTD_HOST_PREFIX_VERSION = rust-std-$(RUST_WASM_VERSION)-wasm32-unknown-unknown
RUST_WASM_LIBSTD_HOST_PREFIX = rust-std-wasm32-unknown-unknown

define RUST_WASM_LIBSTD_EXTRACT
	mkdir -p $(@D)/std
	$(foreach f,$(RUST_WASM_SOURCE), \
		$(call suitable-extractor,$(f)) $(RUST_WASM_DL_DIR)/$(f) | \
			$(TAR) -C $(@D)/std $(TAR_OPTIONS) -
	)
			
endef

RUST_WASM_POST_EXTRACT_HOOKS += RUST_WASM_LIBSTD_EXTRACT

define RUST_WASM_INSTALL_TARGET_CMDS
    cp -r $(RUST_WASM_SRCDIR)/std/$(RUST_WASM_LIBSTD_HOST_PREFIX_VERSION)/$(RUST_WASM_LIBSTD_HOST_PREFIX)/lib/rustlib/* \
            $(HOST_DIR)/lib/rustlib/

endef


$(eval $(generic-package))
