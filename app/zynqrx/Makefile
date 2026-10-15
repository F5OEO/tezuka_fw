# zynqrx: DVB-S / DVB-S2 demodulator for 32-bit ARM (Zynq, Cortex-A9 + NEON). No dependencies beyond the C++ standard library and pthreads.
#
#   Zynq (Buildroot SDK):  . <sdk>/host/environment-setup && make -j4
#   This computer (test):  make -j4 HOST=1
#   Sampling profiler:     make -j4 PROF=1      (static; run with DVBS_PROF=out.txt, resolve with scripts/prof_report.py)
#
#   libiio (live input, rx --iio): IIO=1 (default for the board; IIO=1 with HOST=1 needs libiio-dev). Objects are not rebuilt when IIO changes: make clean.
#   libmosquitto (MQTT control and status in longmynd's scheme, rx --mqtt): MQTT=1 (default for the board; needs libmosquitto-dev with HOST=1). PROF=1 builds are static: use IIO=0 MQTT=0.
# The result is build/zynqrx (build-host/zynqrx with HOST=1). B=<dir> changes the build folder.
HOST ?=
ifeq ($(HOST),1)
  ARCH_FLAGS ?= -march=native
  FASTMATH ?=
  B ?= build-host
  IIO ?= 0
  MQTT ?= 0
else
  ifeq ($(origin CXX),default)
    CXX := arm-linux-g++       # the SDK's environment-setup exports CXX; without it use the cross compiler from PATH
  endif
  ARCH_FLAGS ?= -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
  FASTMATH ?= -funsafe-math-optimizations      # lets GCC vectorise float loops for 32-bit NEON
  B ?= build
  IIO ?= 1
  MQTT ?= 1
endif

SRC_FILES := $(wildcard src/*.cpp) tools/zynqrx.cpp $(if $(PROF),scripts/prof_sampler.cpp)
OBJS := $(patsubst %.cpp,$(B)/%.o,$(notdir $(SRC_FILES)))
CXXFLAGS_ALL := $(if $(PROF),-fno-pie) -std=c++20 -O3 -DNDEBUG $(ARCH_FLAGS) -fcx-limited-range $(FASTMATH) -Iinclude -Isrc -DONAIR_VERSION=\"zynqrx\" $(if $(filter 1,$(IIO)),-DZYNQRX_IIO) $(if $(filter 1,$(MQTT)),-DZYNQRX_MQTT) $(CXXFLAGS)
vpath %.cpp src tools scripts

$(B)/zynqrx: $(OBJS)
	$(CXX) -o $@ $^ -lpthread $(if $(filter 1,$(IIO)),-liio) $(if $(filter 1,$(MQTT)),-lmosquitto) $(LDFLAGS) $(if $(PROF),-static -no-pie)

$(B)/%.o: %.cpp | $(B)
	$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

$(B):
	mkdir -p $(B)

clean:
	rm -rf build build-host

.PHONY: clean
