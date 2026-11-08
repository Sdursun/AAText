# harfbuzz.library - HarfBuzz for AmigaOS 3.0+ (68020+), see HARFBUZZ_PLAN.md
#
# Stage A1: HarfBuzz as a static object and the hbinfo test program.
# Build in the amiga-gcc container, from the repository root:
#   .\build.ps1 -C hblib
# HarfBuzz source: sh tools/fetch-harfbuzz.sh

HB_VER   := 15.1.0
HB_DIR   ?= ../third_party/harfbuzz-$(HB_VER)
BUILDDIR := build/68020

CC  := /opt/amiga/bin/m68k-amigaos-gcc
CXX := /opt/amiga/bin/m68k-amigaos-g++

# HB_MINI: OpenType shaping only (no AAT, no legacy fonts, no OFF 5th
# edition). HB_NO_MT: no atomics; the library serialises all calls.
# The rest drops what has no use on AmigaOS: files, environment,
# locale, atexit, errno.
HB_DEFS := -DHB_MINI -DHB_NO_MT \
           -DHB_NO_OPEN -DHB_NO_MMAP -DHB_NO_GETENV -DHB_NO_SETLOCALE \
           -DHB_NO_ATEXIT -DHB_NO_ERRNO \
           -Dhb_malloc_impl=hb_malloc_impl -Dhb_calloc_impl=hb_calloc_impl \
           -Dhb_realloc_impl=hb_realloc_impl -Dhb_free_impl=hb_free_impl

CXXFLAGS := -m68020 -noixemul -Os -std=c++11 -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -ffunction-sections -fdata-sections \
            -Wno-format -Wno-attributes $(HB_DEFS)
CFLAGS   := -m68020 -noixemul -O2 -Wall -Wextra -Wno-unused-parameter \
            -I$(HB_DIR)/src -Isrc
LDFLAGS  := -m68020 -noixemul -Wl,--gc-sections

.PHONY: all clean
all: $(BUILDDIR)/hbinfo

$(BUILDDIR):
	mkdir -p $@

$(BUILDDIR)/harfbuzz.o: $(HB_DIR)/src/harfbuzz.cc | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILDDIR)/hbmem.o: src/hbmem.c src/hbmem.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/hbinfo: tests/hbinfo.c $(BUILDDIR)/harfbuzz.o $(BUILDDIR)/hbmem.o
	$(CXX) $(LDFLAGS) -x c $(CFLAGS) -o $@ tests/hbinfo.c -x none \
		$(BUILDDIR)/harfbuzz.o $(BUILDDIR)/hbmem.o -lm
	/opt/amiga/bin/m68k-amigaos-size $(BUILDDIR)/harfbuzz.o $@
	@ls -l $@

clean:
	rm -rf build
