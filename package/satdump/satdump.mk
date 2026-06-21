################################################################################
#
# soapysdr
#
################################################################################

SATDUMP_VERSION = master
SATDUMP_SITE = $(call github,SatDump,SatDump,$(SATDUMP_VERSION))
SATDUMP_INSTALL_STAGING = YES

SATDUMP_DEPENDENCIES = volk nng libpng tiff zstd libiio

# Fix: Surgically remove the exact lines forcing -march=native
define SATDUMP_REMOVE_MARCH_NATIVE
	$(SED) '/-march=native/d' $(@D)/CMakeLists.txt
endef

# Fix: remove jemalloc linkage (crashes on embedded targets due to allocator mismatch)
define SATDUMP_DISABLE_JEMALLOC
	$(SED) '/find_package(Jemalloc/d; /JEMALLOC_/d' $(@D)/src-core/CMakeLists.txt
endef

# Fix: SIOF — RESOURCES_PATH (satdump_varst.cpp) must init before RESPATH (satdump_vars.cpp)
# satdump_vars.cpp sorts before satdump_varst.cpp alphabetically, so it links first.
# Use GCC init_priority to override: lower number = earlier initialization.
define SATDUMP_FIX_INIT_ORDER
	$(SED) 's/std::string RESOURCES_PATH = /std::string RESOURCES_PATH __attribute__((init_priority(200))) = /; s/std::string LIBRARIES_PATH = /std::string LIBRARIES_PATH __attribute__((init_priority(200))) = /' $(@D)/src-core/satdump_varst.cxx
	$(SED) 's/std::string RESPATH = /std::string RESPATH __attribute__((init_priority(400))) = /; s/std::string LIBPATH = /std::string LIBPATH __attribute__((init_priority(400))) = /' $(@D)/src-core/satdump_vars.cpp
endef

# Fix: IERS and Kepler auto-update tasks fire immediately on first run (last_run=0 is always
# past), trigger network calls to iers.org / celestrak.org that block and crash on an
# embedded target with no internet access. Disable both schedulers at source level.
define SATDUMP_DISABLE_AUTO_UPDATES
	$(SED) 's/bool honor_setting = true;/bool honor_setting = false;/' $(@D)/src-core/db/iers/iers_handler.cpp
	$(SED) 's/bool honor_setting = true;/bool honor_setting = false;/' $(@D)/src-core/db/kepler/kepler_handler.cpp
endef

# Fix: libad9361 is not available in the sysroot. The plugin uses ad9361_set_bb_rate()
# but sampling_frequency is already set via IIO on the preceding line — drop the call
# and the include, then build with libiio only.
define SATDUMP_FIX_PLUTOSDR_DEPS
	$(SED) 's/(IIO_LIBRARY AND AD9361_LIBRARY)/(IIO_LIBRARY)/' \
		$(@D)/plugins/sdr_sources/plutosdr_sdr_support/CMakeLists.txt
	$(SED) 's/ \$${AD9361_LIBRARY}//' \
		$(@D)/plugins/sdr_sources/plutosdr_sdr_support/CMakeLists.txt
	$(SED) '/find_library(AD9361_LIBRARY/d' \
		$(@D)/plugins/sdr_sources/plutosdr_sdr_support/CMakeLists.txt
	$(SED) '/#include.*ad9361/d' \
		$(@D)/plugins/sdr_sources/plutosdr_sdr_support/plutosdr_sdr.h
	$(SED) '/ad9361_set_bb_rate/d' \
		$(@D)/plugins/sdr_sources/plutosdr_sdr_support/plutosdr_sdr.cpp
endef

# Fix: make DVB-S RRC filter optional — rrc_alpha=0 bypasses it (agc -> pll directly)
# When RRC is disabled there is no need for 2× oversampling; force MIN_SPS=MAX_SPS=1.0
# so the base demod sets final_samplerate = symbolrate and no upsampling is inserted.
define SATDUMP_DVBS_OPTIONAL_RRC
	$(SED) 's/rrc = std::make_shared<dsp::FIRBlock/if (d_rrc_alpha > 0) rrc = std::make_shared<dsp::FIRBlock/' \
		$(@D)/plugins/dvb_support/dvbs/module_dvbs_demod.cpp
	$(SED) 's/pll = std::make_shared<dsp::CostasLoopBlock>(rrc->output_stream/pll = std::make_shared<dsp::CostasLoopBlock>(d_rrc_alpha > 0 ? rrc->output_stream : agc->output_stream/' \
		$(@D)/plugins/dvb_support/dvbs/module_dvbs_demod.cpp
	$(SED) 's/rrc->start()/if (rrc) rrc->start()/' \
		$(@D)/plugins/dvb_support/dvbs/module_dvbs_demod.cpp
	$(SED) 's/rrc->stop()/if (rrc) rrc->stop()/' \
		$(@D)/plugins/dvb_support/dvbs/module_dvbs_demod.cpp
	$(SED) 's/BaseDemodModule::initb();/if (d_rrc_alpha == 0) MIN_SPS = MAX_SPS = 1.0;\n                BaseDemodModule::initb();/' \
		$(@D)/plugins/dvb_support/dvbs/module_dvbs_demod.cpp
endef

SATDUMP_POST_PATCH_HOOKS += SATDUMP_REMOVE_MARCH_NATIVE SATDUMP_DISABLE_JEMALLOC SATDUMP_FIX_INIT_ORDER SATDUMP_DISABLE_AUTO_UPDATES SATDUMP_FIX_PLUTOSDR_DEPS SATDUMP_DVBS_OPTIONAL_RRC

define SATDUMP_REMOVE_MAPS
	rm -rf $(TARGET_DIR)/usr/share/satdump/maps
endef

# Remove all SDR source plugins except plutosdr (stale .so files from previous builds)
define SATDUMP_REMOVE_UNUSED_SDR_PLUGINS
	find $(TARGET_DIR)/usr/lib/satdump/plugins -name 'lib*_sdr_support.so' \
		! -name 'libplutosdr_sdr_support.so' -delete 2>/dev/null || true
	find $(TARGET_DIR)/usr/lib/satdump/plugins \
		-name 'libspyserver_support.so' \
		-o -name 'librtltcp_support.so' \
		-o -name 'libsdrpp_server_support.so' \
		-o -name 'libnet_source_support.so' \
		-o -name 'libremote_sdr_support.so' \
		| xargs rm -f 2>/dev/null || true
endef

SATDUMP_POST_INSTALL_TARGET_HOOKS += SATDUMP_REMOVE_MAPS SATDUMP_REMOVE_UNUSED_SDR_PLUGINS

SATDUMP_CONF_OPTS = -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=ON -DBUILD_GUI=OFF \
-DPLUGINS_ALL=OFF -DPLUGIN_DVB=ON -DBUILD_ZIQ=OFF -DBUILD_OPENCL=OFF -DBUILD_OPENMP=OFF \
-DANGELSCRIPT_GENERIC_ONLY=ON \
-DPLUGIN_AIRSPY_SDR_SUPPORT=OFF \
-DPLUGIN_HYDRASDR_SDR_SUPPORT=OFF \
-DPLUGIN_AIRSPYHF_SDR_SUPPORT=OFF \
-DPLUGIN_HACKRF_SDR_SUPPORT=OFF \
-DPLUGIN_LIMESDR_SDR_SUPPORT=OFF \
-DPLUGIN_SDRPLAY_SDR_SUPPORT=OFF \
-DPLUGIN_BLADERF_SDR_SUPPORT=OFF \
-DPLUGIN_LITEXM2SDR_SDR_SUPPORT=OFF \
-DPLUGIN_USRP_SDR_SUPPORT=OFF \
-DPLUGIN_RTLSDR_SDR_SUPPORT=OFF \
-DPLUGIN_MIRISDR_SDR_SUPPORT=OFF \
-DPLUGIN_AARONIA_SDR_SUPPORT=OFF \
-DPLUGIN_RFNM_SDR_SUPPORT=OFF \
-DPLUGIN_FOBOSSDR_SDR_SUPPORT=OFF \
-DPLUGIN_SPYSERVER_SUPPORT=OFF \
-DPLUGIN_RTLTCP_SUPPORT=OFF \
-DPLUGIN_SDRPP_SERVER_SUPPORT=OFF \
-DPLUGIN_NET_SOURCE_SDR_SUPPORT=OFF \
-DPLUGIN_REMOTE_SDR_SUPPORT=OFF \
-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -DANGELSCRIPT_GENERIC_ONLY"

SATDUMP_INSTALL_STAGING = YES


$(eval $(cmake-package))

