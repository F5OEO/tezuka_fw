HAMLIB_VERSION = 4.5.5
HAMLIB_SITE = https://github.com/Hamlib/Hamlib
HAMLIB_SITE_METHOD = git
HAMLIB_AUTORECONF=YES

$(eval $(autotools-package))

