# harfbuzz.library - HarfBuzz for AmigaOS 3.0+ (68020+), see HARFBUZZ_PLAN.md
#
#   make              build/68020/harfbuzz.library and the test programs
#   make DEBUG=1      debug build: serial kprintf output, stack measuring
#   make headers      regenerate src/gen/ and include/ from src/harfbuzz.funcs
#
# Build in the amiga-gcc container, from the repository root:
#   .\build.ps1 -C hblib
# HarfBuzz source: sh tools/fetch-harfbuzz.sh

HB_VER   := 15.1.0
HB_DIR   ?= ../third_party/harfbuzz-$(HB_VER)
DEBUG    ?= 0
BUILDDIR := build/68020$(if $(filter 1,$(DEBUG)),-debug,)
PYTHON   ?= python3

PREFIX := /opt/amiga/bin/m68k-amigaos-
CC     := $(PREFIX)gcc
CXX    := $(PREFIX)g++
STRIP  := $(PREFIX)strip

# HB_MINI: OpenType shaping only (no AAT, no legacy fonts, no OFF 5th
# edition). HB_NO_MT: no atomics; the library serialises all calls.
# The rest drops what has no use on AmigaOS: files, environment,
# locale, atexit, errno; NDEBUG: no assert() (stdio).
HB_DEFS := -DHB_MINI -DHB_NO_MT -DNDEBUG \
           -DHB_NO_OPEN -DHB_NO_MMAP -DHB_NO_GETENV -DHB_NO_SETLOCALE \
           -DHB_NO_ATEXIT -DHB_NO_ERRNO \
           -Dhb_malloc_impl=hb_malloc_impl -Dhb_calloc_impl=hb_calloc_impl \
           -Dhb_realloc_impl=hb_realloc_impl -Dhb_free_impl=hb_free_impl

ARCH     := -m68020 -noixemul
CXXFLAGS := $(ARCH) -Os -std=c++11 -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -ffunction-sections -fdata-sections \
            -Wno-format -Wno-attributes $(HB_DEFS)
CFLAGS   := $(ARCH) -O2 -fomit-frame-pointer -fno-common -Wall -Wextra \
            -Wno-unused-parameter -I$(HB_DIR)/src -Isrc -Iinclude
LIBFLAGS := $(CFLAGS) $(if $(filter 1,$(DEBUG)),-DDEBUG,)
LDFLAGS  := $(ARCH) -Wl,--gc-sections

TARGET := $(BUILDDIR)/harfbuzz.library
# start.o must come first: its code runs if someone starts the library.
# --gc-sections drops unused HarfBuzz code; RomTag (nothing refers to it)
# is the root that keeps the function table and so every wrapper.
LIB_OBJS := $(BUILDDIR)/start.o $(BUILDDIR)/libinit.o $(BUILDDIR)/wrappers.o \
            $(BUILDDIR)/hbmem.o $(BUILDDIR)/libcstub.o $(BUILDDIR)/harfbuzz.o

GEN      := src/gen/functable.inc src/gen/wrappers.c
GEN_HDRS := include/fd/harfbuzz_lib.fd include/inline/harfbuzz.h \
            include/proto/harfbuzz.h

.PHONY: all clean headers
all: $(TARGET) $(BUILDDIR)/hbinfo $(BUILDDIR)/hbinfo_lib

$(BUILDDIR):
	mkdir -p $@

headers $(GEN) $(GEN_HDRS) &: src/harfbuzz.funcs tools/hbfuncs.py
	mkdir -p build && $(PYTHON) tools/hbfuncs.py src/harfbuzz.funcs src/gen include > build/lvo.txt

$(TARGET): $(LIB_OBJS)
	$(CC) $(ARCH) -nostartfiles -Wl,--gc-sections -Wl,-u,_RomTag \
		-Wl,-Map,$(BUILDDIR)/harfbuzz.map -o $@.db $(LIB_OBJS) -lm -lgcc
	$(STRIP) --strip-all -o $@ $@.db
	@ls -l $@

$(BUILDDIR)/harfbuzz.o: $(HB_DIR)/src/harfbuzz.cc Makefile | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/libinit.o: src/libinit.c src/lib.h src/hbmem.h $(GEN) | $(BUILDDIR)
	$(CC) $(LIBFLAGS) -c -o $@ $<

$(BUILDDIR)/wrappers.o: src/gen/wrappers.c src/lib.h | $(BUILDDIR)
	$(CC) $(LIBFLAGS) -c -o $@ $<

$(BUILDDIR)/hbmem.o: src/hbmem.c src/hbmem.h | $(BUILDDIR)
	$(CC) $(LIBFLAGS) -c -o $@ $<

$(BUILDDIR)/libcstub.o: src/libcstub.c | $(BUILDDIR)
	$(CC) $(LIBFLAGS) -c -o $@ $<

$(BUILDDIR)/start.o: src/start.s | $(BUILDDIR)
	$(CC) $(ARCH) -c -o $@ $<

# test program, HarfBuzz linked in
$(BUILDDIR)/hbinfo: tests/hbinfo.c $(BUILDDIR)/harfbuzz.o $(BUILDDIR)/hbmem.o
	$(CXX) $(LDFLAGS) -x c $(CFLAGS) -o $@ tests/hbinfo.c -x none \
		$(BUILDDIR)/harfbuzz.o $(BUILDDIR)/hbmem.o -lm

# the same test through harfbuzz.library
$(BUILDDIR)/hbinfo_lib: tests/hbinfo.c $(GEN_HDRS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -DUSE_LIBRARY -o $@ tests/hbinfo.c -lm

clean:
	rm -rf build
