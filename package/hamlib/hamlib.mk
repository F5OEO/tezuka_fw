HAMLIB_VERSION = 4.7.1
HAMLIB_SITE = https://github.com/Hamlib/Hamlib/archive/refs/tags
HAMLIB_SOURCE = $(HAMLIB_VERSION).tar.gz
HAMLIB_AUTORECONF=YES

$(eval $(autotools-package))

