################################################################################
#
# MaiaKmod
#
################################################################################

FL2K_KMOD_VERSION = master
FL2K_KMOD_SOURCE = master.tar.gz
FL2K_KMOD_SITE = https://github.com/ReachableCEO/fl2000_drm/archive/refs/heads
FL2K_KMOD_MODULE_DEPENDENCIES = linux
FL2K_KMOD_MODULE_SUBDIRS = .
FL2K_KMOD_MODULE_MAKE_OPTS = KVERSION=$(LINUX_VERSION_PROBED)

define FL2K_KMOD_POST_PATCH_FIXUP
	$(SED) 's|clock_mil \* (mode->htotal + d) / mode->htotal|div_u64(clock_mil * (mode->htotal + d), mode->htotal)|' \
		$(@D)/fl2000_drm.c
endef
FL2K_KMOD_POST_PATCH_HOOKS += FL2K_KMOD_POST_PATCH_FIXUP

$(eval $(kernel-module))
$(eval $(generic-package))