################################################################################
#
# MaiaKmod
#
################################################################################

MAIA_KMOD_VERSION = 0.8.1
MAIA_KMOD_SOURCE = maia-sdr-$(MAIA_KMOD_VERSION).tar.gz
MAIA_KMOD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/v$(MAIA_KMOD_VERSION)
#MAIA_KMOD_SITE = https://github.com/maia-sdr/maia-sdr/archive/refs/tags/
MAIA_KMOD_MODULE_SUBDIRS = maia-kmod
MAIA_KMOD_MODULE_DEPENDENCIES = linux
MAIA_KMOD_MODULE_MAKE_OPTS = KVERSION=$(LINUX_VERSION_PROBED) KBUILD_MODPOST_WARN=1
#MAIA_KMOD_MAKE_OPTS=KBUILD_MODPOST_WARN=1 ---> TO BE INSPECTED
define MAIA_KMOD_MODULE_BUILD_CMDS
	$(MAKE) -C $(@D) $(LINUX_MAKE_FLAGS) $(LINUX_MAKE_FLAGS) M=$(@D) KERNELDIR=$(LINUX_DIR)
endef

$(eval $(kernel-module))
$(eval $(generic-package))