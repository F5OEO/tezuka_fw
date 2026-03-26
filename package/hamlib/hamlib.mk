HAMLIB_VERSION = 4.5.5
HAMLIB_SITE = https://github.com/Hamlib/Hamlib/archive/refs/tags
HAMLIB_SOURCE = $(HAMLIB_VERSION).tar.gz
HAMLIB_AUTORECONF=YES

$(eval $(autotools-package))

