################################################################################
#
# libiio — latest main (overrides Buildroot built-in v0.26)
#
################################################################################

LIBIIO_VERSION = 4cc1ac78fafae04533d90c7f973efd7a38b88dd2
LIBIIO_SITE = $(call github,analogdevicesinc,libiio,$(LIBIIO_VERSION))
LIBIIO_INSTALL_STAGING = YES
LIBIIO_LICENSE = LGPL-2.1+
LIBIIO_LICENSE_FILES = COPYING.txt

LIBIIO_CONF_OPTS = -DENABLE_IPV6=ON \
	-DWITH_LOCAL_BACKEND=$(if $(BR2_PACKAGE_LIBIIO_LOCAL_BACKEND),ON,OFF) \
	-DWITH_NETWORK_BACKEND=$(if $(BR2_PACKAGE_LIBIIO_NETWORK_BACKEND),ON,OFF) \
	-DINSTALL_UDEV_RULE=$(if $(BR2_PACKAGE_HAS_UDEV),ON,OFF) \
	-DWITH_TESTS=$(if $(BR2_PACKAGE_LIBIIO_TESTS),ON,OFF) \
	-DWITH_DOC=OFF \
	-DWITH_ZSTD=$(if $(BR2_PACKAGE_ZSTD),ON,OFF) \
	-DWITH_MODULES=OFF \
	-DLIBIIO_COMPAT=ON

ifeq ($(BR2_TOOLCHAIN_HAS_THREADS),y)
LIBIIO_CONF_OPTS += -DNO_THREADS=OFF
else
LIBIIO_CONF_OPTS += -DNO_THREADS=ON
endif

ifeq ($(BR2_PACKAGE_LIBIIO_XML_BACKEND),y)
LIBIIO_DEPENDENCIES += libxml2
LIBIIO_CONF_OPTS += -DWITH_XML_BACKEND=ON
else
LIBIIO_CONF_OPTS += -DWITH_XML_BACKEND=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_USB_BACKEND),y)
LIBIIO_DEPENDENCIES += libusb
LIBIIO_CONF_OPTS += -DWITH_USB_BACKEND=ON
else
LIBIIO_CONF_OPTS += -DWITH_USB_BACKEND=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_SERIAL_BACKEND),y)
LIBIIO_DEPENDENCIES += libserialport
LIBIIO_CONF_OPTS += -DWITH_SERIAL_BACKEND=ON
else
LIBIIO_CONF_OPTS += -DWITH_SERIAL_BACKEND=OFF
endif

ifeq ($(BR2_PACKAGE_ZSTD),y)
LIBIIO_DEPENDENCIES += zstd
endif

ifeq ($(BR2_PACKAGE_LIBIIO_IIOD),y)
LIBIIO_DEPENDENCIES += host-flex host-bison
LIBIIO_CONF_OPTS += -DWITH_IIOD=ON
else
LIBIIO_CONF_OPTS += -DWITH_IIOD=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_IIOD_USBD),y)
LIBIIO_DEPENDENCIES += libaio
LIBIIO_CONF_OPTS += -DWITH_IIOD_USBD=ON
else
LIBIIO_CONF_OPTS += -DWITH_IIOD_USBD=OFF
endif

ifeq ($(BR2_PACKAGE_LIBAIO),y)
LIBIIO_DEPENDENCIES += libaio
LIBIIO_CONF_OPTS += -DWITH_AIO=ON
else
LIBIIO_CONF_OPTS += -DWITH_AIO=OFF
endif

ifeq ($(BR2_PACKAGE_AVAHI_LIBAVAHI_CLIENT),y)
LIBIIO_DEPENDENCIES += avahi
LIBIIO_CONF_OPTS += -DHAVE_DNS_SD=ON
else
LIBIIO_CONF_OPTS += -DHAVE_DNS_SD=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_BINDINGS_PYTHON),y)
LIBIIO_DEPENDENCIES += host-python-setuptools python3
LIBIIO_CONF_OPTS += \
	-DPYTHON_BINDINGS=ON \
	-DPYTHON_EXECUTABLE=$(HOST_DIR)/bin/python3
else
LIBIIO_CONF_OPTS += -DPYTHON_BINDINGS=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_BINDINGS_CSHARP),y)
define LIBIIO_INSTALL_CSHARP_BINDINGS_TO_TARGET
	$(HOST_DIR)/bin/gacutil -root $(TARGET_DIR)/usr/lib -i \
		$(TARGET_DIR)/usr/lib/cli/libiio-sharp-$(LIBIIO_VERSION)/libiio-sharp.dll
endef
define LIBIIO_INSTALL_CSHARP_BINDINGS_TO_STAGING
	$(HOST_DIR)/bin/gacutil -root $(STAGING_DIR)/usr/lib -i \
		$(STAGING_DIR)/usr/lib/cli/libiio-sharp-$(LIBIIO_VERSION)/libiio-sharp.dll
endef
LIBIIO_POST_INSTALL_TARGET_HOOKS += LIBIIO_INSTALL_CSHARP_BINDINGS_TO_TARGET
LIBIIO_POST_INSTALL_STAGING_HOOKS += LIBIIO_INSTALL_CSHARP_BINDINGS_TO_STAGING
LIBIIO_DEPENDENCIES += mono
LIBIIO_CONF_OPTS += -DCSHARP_BINDINGS=ON
else
LIBIIO_CONF_OPTS += -DCSHARP_BINDINGS=OFF
endif

ifeq ($(BR2_PACKAGE_LIBIIO_IIOD),y)
define LIBIIO_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_PLUTOSDR_PATH)/package/libiio/S60iiod \
		$(TARGET_DIR)/etc/init.d/S60iiod
endef
endif

$(eval $(cmake-package))

# libiio 1.0 moved headers to include/iio/ subdir. Create compat symlinks
# so downstream packages using find_path(iio.h) or #include <iio.h> work.
# iio.h uses the v0.26 compat header so old API callers (soapyplutosdr) compile
# against the old declarations and link against libiio.so.0 (compat layer).
define LIBIIO_CREATE_COMPAT_SYMLINKS
	cp $(BR2_EXTERNAL_PLUTOSDR_PATH)/package/libiio/iio-compat.h \
		$(STAGING_DIR)/usr/include/iio.h
	ln -sf iio/iio-debug.h $(STAGING_DIR)/usr/include/iio-debug.h
	ln -sf iio/iio-lock.h $(STAGING_DIR)/usr/include/iio-lock.h
	ln -sf iio/iiod-client.h $(STAGING_DIR)/usr/include/iiod-client.h
	ln -sf iio/iio-backend.h $(STAGING_DIR)/usr/include/iio-backend.h
	# Both main and compat libs have OUTPUT_NAME=iio but different
	# SOVERSIONs (1 vs 0). CMake's libiio.so symlink points to
	# libiio.so.1 (new API) which lacks old symbols. Fix: make the
	# linker symlink point to the compat library (libiio.so.0) which
	# exports all old symbols and forwards to libiio.so.1 at runtime.
	rm -f $(STAGING_DIR)/usr/lib/libiio.so
	ln -sf libiio.so.0 $(STAGING_DIR)/usr/lib/libiio.so
endef

LIBIIO_POST_INSTALL_STAGING_HOOKS += LIBIIO_CREATE_COMPAT_SYMLINKS

# Same fix for target — ensure libiio.so linker symlink points to compat.
define LIBIIO_FIX_TARGET_LINKER_SYMLINK
	rm -f $(TARGET_DIR)/usr/lib/libiio.so
	ln -sf libiio.so.0 $(TARGET_DIR)/usr/lib/libiio.so
endef

LIBIIO_POST_INSTALL_TARGET_HOOKS += LIBIIO_FIX_TARGET_LINKER_SYMLINK
