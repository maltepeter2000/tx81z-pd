# Build the tx81z~ Pd external.
#   macOS:      make            -> tx81z~.pd_darwin
#   Linux/Bela: make            -> tx81z~.pd_linux
# On Bela the libpd headers of the board are used automatically and the
# code is tuned for the Cortex-A8 (NEON). Override with PD_INCLUDE=...

ROOT := ..
YMFM := $(ROOT)/third_party/ymfm/src

# prefer the Pd headers installed on the system (Bela: libpd), else the bundled one
ifneq ($(wildcard /usr/local/include/libpd/m_pd.h),)
  PD_INCLUDE ?= /usr/local/include/libpd
else
  PD_INCLUDE ?= $(ROOT)/third_party/pd
endif

SRC := tx81z~.cpp \
       $(ROOT)/src/tx81z.cpp \
       $(ROOT)/src/cpu/hd6303x.cpp \
       $(YMFM)/ymfm_opz.cpp $(YMFM)/ymfm_misc.cpp $(YMFM)/ymfm_adpcm.cpp $(YMFM)/ymfm_ssg.cpp

CXXFLAGS ?= -O3
CXXFLAGS += -std=c++14 -fPIC -fvisibility=hidden -I$(PD_INCLUDE) -I$(ROOT)/src -I$(YMFM)

UNAME := $(shell uname -s)
ARCH  := $(shell uname -m)
ifeq ($(UNAME),Darwin)
  EXT := pd_darwin
  LDFLAGS += -bundle -undefined dynamic_lookup -arch arm64 -arch x86_64
  CXXFLAGS += -arch arm64 -arch x86_64 -mmacosx-version-min=10.13
else
  EXT := pd_linux
  LDFLAGS += -shared
  ifeq ($(ARCH),armv7l)
    # Bela (BeagleBone / PocketBeagle, Cortex-A8)
    CXXFLAGS += -march=armv7-a -mtune=cortex-a8 -mfloat-abi=hard -mfpu=neon -ftree-vectorize
  endif
endif

CXX ?= c++

all: tx81z~.$(EXT)

tx81z~.$(EXT): $(SRC) $(ROOT)/src/*.h $(ROOT)/src/cpu/*.h
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) -o $@
	@echo "built $@ (Pd headers: $(PD_INCLUDE))"

clean:
	rm -f tx81z~.pd_darwin tx81z~.pd_linux

.PHONY: all clean
