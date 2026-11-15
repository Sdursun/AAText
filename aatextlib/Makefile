# freetype2.library 2.15 - AAText's library: FreeType 2.15.0 for AmigaOS 3
#
#   make              68020 build:  build/68020/freetype2.library
#   make CPU=68060    68060 build:  build/68060/freetype2.library
#   make DEBUG=1      debug build with serial kprintf output
#   make test         test programs (build/<cpu>/tests/)
#   make dist         Aminet archive in dist/
#
# Normally invoked inside Docker via build.ps1.

PREFIX  ?= /opt/amiga/bin/m68k-amigaos-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc
AR      := $(PREFIX)ar
STRIP   := $(PREFIX)strip
PYTHON  ?= python3

CPU     ?= 68020
DEBUG   ?= 0

FT_VER  := 2.15.0
# the same FreeType source as AAText's own build
FT_DIR  ?= ../third_party/freetype-$(FT_VER)

BUILDDIR := build/$(CPU)$(if $(filter 1,$(DEBUG)),-debug,)
TARGET   := $(BUILDDIR)/freetype2.library

ARCHFLAGS := -m$(CPU) -noixemul
FTDEFS    := -DFT2_BUILD_LIBRARY \
             -DFT_CONFIG_OPTIONS_H="<ft2_option.h>" \
             -DFT_CONFIG_MODULES_H="<ft2_module.h>"
INCLUDES  := -Isrc -Iinclude -I$(FT_DIR)/include
CFLAGS    := $(ARCHFLAGS) -O2 -fomit-frame-pointer -fno-common \
             -Wall -Wno-unused-parameter -Wno-attributes $(INCLUDES) $(FTDEFS)
FT_CFLAGS := $(ARCHFLAGS) -O2 -fomit-frame-pointer -fno-common \
             -Wno-attributes $(INCLUDES) $(FTDEFS)
ASFLAGS   := $(ARCHFLAGS)
LDFLAGS   := $(ARCHFLAGS) -nostartfiles -Wl,-Map,$(BUILDDIR)/aatext.map
LIBS      := -lgcc

ifeq ($(DEBUG),1)
CFLAGS    += -DDEBUG
LIBS      += -ldebug
endif

# start.o must come first: its code runs if someone starts the library
LIB_OBJS := $(BUILDDIR)/start.o $(BUILDDIR)/libinit.o $(BUILDDIR)/bullet.o \
            $(BUILDDIR)/ftsystem.o

FT_SRCS := $(addprefix $(FT_DIR)/src/, \
           base/ftbase.c base/ftinit.c base/ftdebug.c base/ftsynth.c \
           base/ftbitmap.c base/ftglyph.c base/ftbbox.c base/ftstroke.c \
           base/ftmm.c base/ftfstype.c \
           autofit/autofit.c truetype/truetype.c type1/type1.c cff/cff.c \
           cid/type1cid.c pfr/pfr.c type42/type42.c winfonts/winfnt.c \
           pcf/pcf.c bdf/bdf.c psaux/psaux.c psnames/psnames.c \
           pshinter/pshinter.c sfnt/sfnt.c smooth/smooth.c raster/raster.c \
           gzip/ftgzip.c lzw/ftlzw.c cache/ftcache.c)
FT_OBJS := $(addprefix $(BUILDDIR)/ft/,$(notdir $(FT_SRCS:.c=.o)))
FT_LIB  := $(BUILDDIR)/libft.a

GEN      := src/gen/functable.inc
GEN_HDRS := include/fd/freetype2_lib.fd include/clib/freetype2_protos.h \
            include/inline/freetype2.h include/proto/freetype2.h

vpath %.c $(sort $(dir $(FT_SRCS)))

.PHONY: all clean test dist headers

all: $(TARGET)

$(TARGET): $(LIB_OBJS) $(FT_LIB)
	$(CC) $(LDFLAGS) -o $@.db $(LIB_OBJS) $(FT_LIB) $(LIBS)
	$(STRIP) --strip-all -o $@ $@.db
	@ls -l $@

headers $(GEN) $(GEN_HDRS) &: src/freetype2.funcs tools/genfuncs.py
	$(PYTHON) tools/genfuncs.py src/freetype2.funcs src/gen include > build/lvo.txt

$(BUILDDIR)/libinit.o: $(GEN)

$(BUILDDIR)/%.o: src/%.c src/*.h ../src/gposkern.h include/libraries/freetype2.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: src/%.s | $(BUILDDIR)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(FT_LIB): $(FT_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILDDIR)/ft/%.o: %.c src/ft2_option.h src/ft2_module.h | $(BUILDDIR)/ft
	$(CC) $(FT_CFLAGS) -c -o $@ $<

$(BUILDDIR) $(BUILDDIR)/ft $(BUILDDIR)/tests:
	mkdir -p $@

clean:
	rm -rf build/68020 build/68020-debug build/68060 build/68060-debug

# ---- test programs (run on the Amiga) ----------------------------------

TESTS := $(BUILDDIR)/tests/ft2info $(BUILDDIR)/tests/ft2bullet $(BUILDDIR)/tests/ft2dfont \
         $(BUILDDIR)/tests/ft2color

test: $(TESTS)

$(BUILDDIR)/tests/%: tests/%.c $(GEN_HDRS) | $(BUILDDIR)/tests
	$(CC) $(ARCHFLAGS) -O2 -Wall -Iinclude -I$(FT_DIR)/include -o $@ $<

# ---- Aminet archive ----------------------------------------------------
#
# dist/freetype2.lha and dist/freetype2.readme:
#   freetype2/Libs        the library (68020, 68060, debug)
#   freetype2/Tools       ft2color (colour emoji), ft2info, ft2bullet, ft2dfont
#   freetype2/Developer   headers, SDK documents, Examples (the tools' source)

PKG     := freetype2
DISTDIR := dist/$(PKG)
DEV     := $(DISTDIR)/Developer

dist:
	$(MAKE) CPU=68020 DEBUG=0
	$(MAKE) CPU=68060 DEBUG=0
	$(MAKE) CPU=68020 DEBUG=1
	$(MAKE) CPU=68020 DEBUG=0 test
	rm -rf dist
	mkdir -p $(DISTDIR)/Libs $(DISTDIR)/Tools $(DEV)/Examples
	cp build/68020/freetype2.library $(DISTDIR)/Libs/freetype2.library
	cp build/68060/freetype2.library $(DISTDIR)/Libs/freetype2.library.060
	cp build/68020-debug/freetype2.library $(DISTDIR)/Libs/freetype2.library.debug
	cp -r include $(DEV)/include
	cp -r $(FT_DIR)/include/ft2build.h $(FT_DIR)/include/freetype $(DEV)/include/
	rm -rf $(DEV)/include/freetype/internal
	cp src/freetype2.funcs $(DEV)/freetype2.funcs
	cp tests/*.c $(DEV)/Examples/
	cp docs/freetype2_SDK.txt $(DEV)/
	cp build/68020/tests/* $(DISTDIR)/Tools/
	cp docs/freetype2.readme $(DISTDIR)/
	cp docs/freetype2.txt $(DISTDIR)/
	cp $(FT_DIR)/docs/FTL.TXT $(DISTDIR)/FTL.txt
	cp LICENSE $(DISTDIR)/LICENSE.txt
	# acceptance: every example builds from the archive's files alone
	cd $(DEV) && for f in Examples/*.c; do \
		$(CC) -m68020 -O2 -noixemul -Wall -Iinclude -o ../../check-ex $$f || exit 1; \
	done
	rm -f dist/check-ex
	chmod 644 $$(find $(DISTDIR) -type f)
	chmod 755 $(DISTDIR)/Libs/* $(DISTDIR)/Tools/*
	cd dist && lha ao5 --system-kanji-code=utf8 --archive-kanji-code=latin1 \
		$(PKG).lha $(PKG)
	cp docs/freetype2.readme dist/freetype2.readme
	@ls -l dist
