# harfbuzz.library - HarfBuzz for AmigaOS 3.1+ (68020+), see HARFBUZZ_PLAN.md
#
#   make              build/68020/harfbuzz.library, the tests and the example
#   make CPU=68060    build/68060/ (the library only: make CPU=68060 lib)
#   make DEBUG=1      debug build: serial kprintf output, stack measuring
#   make headers      regenerate src/gen/ and include/ from src/harfbuzz.funcs
#   make dist         Aminet archive dist/harfbuzz_lib.lha (checks that the
#                     example builds with the archive's files alone)
#
# Build in the amiga-gcc container, from the repository root:
#   .\build.ps1 -C hblib
# HarfBuzz source: sh tools/fetch-harfbuzz.sh

HB_VER   := 15.1.0
HB_DIR   ?= ../third_party/harfbuzz-$(HB_VER)
DEBUG    ?= 0
CPU      ?= 68020
BUILDDIR := build/$(CPU)$(if $(filter 1,$(DEBUG)),-debug,)
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

ARCH     := -m$(CPU) -noixemul
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

.PHONY: all lib clean headers dist
all: $(TARGET) $(BUILDDIR)/hbinfo $(BUILDDIR)/hbinfo_lib $(BUILDDIR)/hbfuzz \
     $(BUILDDIR)/hbbench $(BUILDDIR)/hbshape
lib: $(TARGET)

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

# malformed fonts (tests/mkpack.py packs them) and speed, through the library
$(BUILDDIR)/hbfuzz: tests/hbfuzz.c $(GEN_HDRS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ tests/hbfuzz.c

$(BUILDDIR)/hbbench: tests/hbbench.c $(GEN_HDRS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ tests/hbbench.c -lm

# the example program
$(BUILDDIR)/hbshape: examples/hbshape.c $(GEN_HDRS) | $(BUILDDIR)
	$(CC) $(ARCH) -O2 -Wall -I$(HB_DIR)/src -Iinclude -o $@ examples/hbshape.c

clean:
	rm -rf build dist

# ---- Aminet archive ----------------------------------------------------
#
# Developer/include gets our headers and those HarfBuzz headers that
# <proto/harfbuzz.h> pulls in (asked from the compiler with -M). Then the
# example is built once more with nothing but the archive's files.

DISTDIR := dist/harfbuzz_lib
DEV     := $(DISTDIR)/Developer

dist:
	$(MAKE) CPU=68020 DEBUG=0
	$(MAKE) CPU=68060 DEBUG=0 lib
	$(MAKE) CPU=68020 DEBUG=1 lib
	rm -rf dist
	mkdir -p $(DISTDIR)/Libs $(DEV)/include $(DEV)/examples $(DISTDIR)/Tests
	cp build/68020/harfbuzz.library $(DISTDIR)/Libs/harfbuzz.library
	cp build/68060/harfbuzz.library $(DISTDIR)/Libs/harfbuzz.library.060
	cp build/68020-debug/harfbuzz.library $(DISTDIR)/Libs/harfbuzz.library.debug
	cp -r include/fd include/inline include/proto $(DEV)/include/
	$(CC) -M -MT x -I$(HB_DIR)/src -Iinclude -x c include/proto/harfbuzz.h \
		| tr ' \\' '\n\n' | grep '^$(HB_DIR)/src/' | sort -u \
		> build/hb-headers.txt
	cp $$(cat build/hb-headers.txt) $(DEV)/include/
	cp src/harfbuzz.funcs $(DEV)/
	cp examples/hbshape.c tests/hbinfo.c tests/hbbench.c tests/hbfuzz.c \
		$(DEV)/examples/
	cp build/68020/hbshape build/68020/hbbench build/68020/hbfuzz \
		$(DISTDIR)/Tests/
	cp build/68020/hbinfo_lib $(DISTDIR)/Tests/hbinfo
	cp docs/harfbuzz_lib.readme docs/harfbuzz_lib_EN.txt $(DISTDIR)/
	iconv -f UTF-8 -t ISO-8859-9 docs/harfbuzz_lib_TR.txt \
		> $(DISTDIR)/harfbuzz_lib_TR.txt
	cp LICENSE $(DISTDIR)/LICENSE.txt
	iconv -f UTF-8 -t ISO-8859-1 $(HB_DIR)/COPYING \
		> $(DISTDIR)/LICENSE.harfbuzz.txt
	cp $(HB_DIR)/src/ms-use/COPYING $(DISTDIR)/LICENSE.ms-use.txt
	# acceptance: the example builds from the archive's files alone
	cd $(DEV) && $(CC) -m68020 -O2 -noixemul -Wall -Iinclude \
		-o ../../check-hbshape examples/hbshape.c
	rm -f dist/check-hbshape
	cd dist && lha ao5 --system-kanji-code=utf8 --archive-kanji-code=latin1 \
		harfbuzz_lib.lha harfbuzz_lib
	cp docs/harfbuzz_lib.readme dist/harfbuzz_lib.readme
	@ls -l dist
