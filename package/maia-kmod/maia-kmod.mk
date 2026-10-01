################################################################################
#
# MaiaKmod
#
################################################################################

# Deliberately pinned to an upstream tagged release rather than the
# F5OEO/maia-sdr fork commit that maia-httpd/maia-wasm use (see
# MAIA_HTTPD_VERSION / MAIA_WASM_VERSION, "must match" -- that rule
# does not apply here). Verified by comparing blob SHAs: this
# package's maia-kmod/ directory is byte-identical between upstream
# tag v0.12.0 and the fork commit maia-httpd/maia-wasm pin, so the
# smaller, more stable upstream tag is the better source -- the fork
# has simply never touched the kmod. Re-verify this by blob SHA
# before ever bumping MAIA_KMOD_VERSION independently of the other
# two, in case that stops being true.
MAIA_KMOD_VERSION = 0.12.0
MAIA_KMOD_SOURCE = maia-sdr-$(MAIA_KMOD_VERSION).tar.gz
MAIA_KMOD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_KMOD_VERSION)
MAIA_KMOD_MODULE_SUBDIRS = maia-kmod
MAIA_KMOD_MODULE_DEPENDENCIES = linux
MAIA_KMOD_MODULE_MAKE_OPTS = KVERSION=$(LINUX_VERSION_PROBED)
# NOTE: upstream commit 6f997c69 (2026-03-25, "maia-kmod: fix kernel
# 6.4+ and 6.11+ API compatibility") landed on main *after* v0.12.0
# and duplicates what local patch 0001-fix-kernel-6.4-compat.patch
# already does here. If MAIA_KMOD_VERSION is ever bumped past
# v0.12.0, check whether that patch is now redundant/conflicting and
# drop it if so.
#MAIA_KMOD_MAKE_OPTS=KBUILD_MODPOST_WARN=1 ---> TO BE INSPECTED
define MAIA_KMOD_MODULE_BUILD_CMDS
	$(MAKE) -C $(@D) $(LINUX_MAKE_FLAGS) M=$(@D) KERNELDIR=$(LINUX_DIR)
endef

$(eval $(kernel-module))
$(eval $(generic-package))