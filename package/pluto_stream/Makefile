# Buildroot-compatible Makefile for pluto-stream
# All toolchain variables (CC, CXX, CFLAGS, LDFLAGS, etc.) are provided
# by Buildroot at build time. Do NOT hardcode cross-compiler paths here.

# Version from git (fallback if not in a git repo)
VER ?= $(shell git describe --tags 2>/dev/null || echo "unknown")
$(shell git log --pretty=format:"%h - %ad : %s" > history.txt 2>/dev/null || true)

INC = -I./include_gse/gse

# Base compiler flags (Buildroot appends its own via CFLAGS / CXXFLAGS)
COMMON_FLAGS = -fpermissive -Wall \
	-Wno-write-strings -Wno-unused-function -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-pointer-arith \
	-Wno-format-zero-length -Wno-sign-compare

# Architecture-specific flags – Buildroot sets these too, but they are
# harmless to repeat and guarantee NEON is enabled.
ARCH_FLAGS = -mfpu=neon -mfloat-abi=hard

EXTRA_CXXFLAGS = $(COMMON_FLAGS) $(ARCH_FLAGS)
EXTRA_LDFLAGS  = -Wa,--noexecstack -Wl,-z,noexecstack

# ---------- targets ----------

TARGETS = pluto_mqtt_ctrl pluto_stream

all: $(TARGETS)

pluto_mqtt_ctrl: pluto_mqtt_ctrl.cpp mymqtt.h mqtthandlecommand.h \
                 mqtthandlecommand.cpp iiofshelper.cpp iiofshelper.h
	$(CXX) $(CXXFLAGS) $(EXTRA_CXXFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) \
		-o $@ \
		pluto_mqtt_ctrl.cpp mqtthandlecommand.cpp iiofshelper.cpp \
		-lm -lrt -lpthread -lmosquitto

pluto_stream: pluto_stream.cpp mymqtt.h mqtthandlestream.h \
              mqtthandlestream.cpp streamstate.h fpgadvbs2.cpp fpgadvbs2.h \
              iiostream.cpp iiostream.h rxstream.cpp rxstream.h \
              txstream.cpp txstream.h iiofshelper.cpp \
              dvbs2neon/dvbs2neon0v43.S tsinputmux.cpp tsinputmux.h \
              gsemux.cpp gsemux.h iqtofft.h iqtofft.cpp \
              ts_util/sdt.cpp ts_util/pcrpts.c \
              dvbsarm/fec100.c dvbsarm/dvbsenco8.s
	$(CXX) $(INC) $(CXXFLAGS) $(EXTRA_CXXFLAGS) $(LDFLAGS) $(EXTRA_LDFLAGS) \
		-DCOMIT_FW=\"$(VER)\" \
		-o $@ \
		gsemux.cpp dvbs2neon/dvbs2neon0v43.S ts_util/sdt.cpp ts_util/pcrpts.c \
		pluto_stream.cpp mqtthandlestream.cpp fpgadvbs2.cpp iiostream.cpp \
		rxstream.cpp txstream.cpp iiofshelper.cpp tsinputmux.cpp iqtofft.cpp \
		dvbsarm/fec100.c dvbsarm/dvbsenco8.s \
		-lm -lrt -lpthread -lmosquitto -lgse -lNE10 

# ---------- install ----------
# $(TARGET_DIR) is provided by Buildroot and points to the target rootfs.
# $(BINDIR) defaults to /usr/bin but can be overridden.

BINDIR ?= /root/dvb
SCRIPTS = qo100initdvb.sh gain.sh mute.sh unmute.sh settxmode.sh \
          initdvb.sh mqtt_ifconfig.sh mqtt_iptable.sh mqtt_route.sh \
          mqtt_reboot.sh mqtt_setcall.sh mqtt_longmynd.sh \
          passthrough.sh agctest.sh watchconsoletx.sh relay.sh

install:
	install -d $(TARGET_DIR)$(BINDIR)
	install -m 0755 pluto_mqtt_ctrl $(TARGET_DIR)$(BINDIR)
	install -m 0755 pluto_stream    $(TARGET_DIR)$(BINDIR)
	for s in $(SCRIPTS); do \
		[ -f $$s ] && install -m 0755 $$s $(TARGET_DIR)$(BINDIR) || true; \
	done
	[ -f history.txt ] && install -m 0644 history.txt $(TARGET_DIR)$(BINDIR) || true

clean:
	rm -f $(TARGETS)
