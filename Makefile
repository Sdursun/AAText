# AAText - antialiased Text() patch for AmigaOS 3.2
#
#   make            release build (68020+)
#   make DEBUG=1    debug build with serial kprintf output
#   make CPU=68060  build for 68060
#
# Normally invoked inside Docker via build.ps1.
# FreeType sources are fetched with tools/fetch-freetype.sh.

PREFIX  ?= /opt/amiga/bin/m68k-amigaos-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc
AR      := $(PREFIX)ar
STRIP   := $(PREFIX)strip

CPU     ?= 68020
DEBUG   ?= 0

FT_VER  := 2.14.3
FT_DIR  := third_party/freetype-$(FT_VER)

BUILDDIR := build/$(CPU)$(if $(filter 1,$(DEBUG)),-debug,)
TARGET   := $(BUILDDIR)/AAText

ARCHFLAGS := -m$(CPU) -noixemul
CFLAGS  := $(ARCHFLAGS) -O2 -fomit-frame-pointer \
           -Wall -Wextra -Wno-unused-parameter -Isrc \
           -I$(FT_DIR)/include -Isrc/ft \
           -DFT_CONFIG_OPTIONS_H="<aa_ftoption.h>" \
           -DFT_CONFIG_MODULES_H="<aa_ftmodule.h>"
FT_CFLAGS := $(ARCHFLAGS) -O2 -fomit-frame-pointer -DFT2_BUILD_LIBRARY -Wno-attributes \
           -I$(FT_DIR)/include -Isrc/ft \
           -DFT_CONFIG_OPTIONS_H="<aa_ftoption.h>" \
           -DFT_CONFIG_MODULES_H="<aa_ftmodule.h>"
ASFLAGS := $(ARCHFLAGS)
LDFLAGS := $(ARCHFLAGS)
LIBS    := -lm

ifeq ($(DEBUG),1)
CFLAGS  += -DDEBUG
endif

SRCS_C := src/main.c src/patch.c src/render.c src/metrics.c src/prefs.c \
          src/glyphs.c src/otag.c src/debug.c
SRCS_S := src/stub.s src/cgx.s
OBJS   := $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS_C)) \
          $(patsubst src/%.s,$(BUILDDIR)/%.o,$(SRCS_S))

# FreeType: single-file module builds
FT_SRCS := $(FT_DIR)/src/base/ftinit.c \
           $(FT_DIR)/src/base/ftbase.c \
           $(FT_DIR)/src/base/ftdebug.c \
           $(FT_DIR)/src/base/ftsynth.c \
           $(FT_DIR)/src/base/ftbitmap.c \
           $(FT_DIR)/src/sfnt/sfnt.c \
           $(FT_DIR)/src/truetype/truetype.c \
           $(FT_DIR)/src/smooth/smooth.c \
           src/ft/aa_ftsystem.c
FT_OBJS := $(addprefix $(BUILDDIR)/ft/,$(notdir $(FT_SRCS:.c=.o)))
FT_LIB  := $(BUILDDIR)/libft.a

vpath %.c $(sort $(dir $(FT_SRCS)))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(FT_LIB)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(FT_LIB) $(LIBS)
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/%.o: src/%.c src/*.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: src/%.s | $(BUILDDIR)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(FT_LIB): $(FT_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILDDIR)/ft/%.o: %.c src/ft/aa_ftoption.h src/ft/aa_ftmodule.h | $(BUILDDIR)/ft
	$(CC) $(FT_CFLAGS) -c -o $@ $<

$(BUILDDIR) $(BUILDDIR)/ft:
	mkdir -p $@

$(FT_DIR)/include/ft2build.h:
	@echo "FreeType sources missing: run tools/fetch-freetype.sh"; exit 1

$(OBJS) $(FT_OBJS): | $(FT_DIR)/include/ft2build.h

clean:
	rm -rf build

# Host-side smoke test (run under vamos, see test.ps1)
TEST_OBJS := $(BUILDDIR)/prefs.o $(BUILDDIR)/glyphs.o $(BUILDDIR)/metrics.o \
             $(BUILDDIR)/otag.o $(BUILDDIR)/debug.o \
             $(BUILDDIR)/stub.o

.PHONY: test
test: $(BUILDDIR)/fttest

$(BUILDDIR)/fttest: tests/fttest.c $(TEST_OBJS) $(FT_LIB)
	$(CC) $(CFLAGS) -o $@ tests/fttest.c $(TEST_OBJS) $(FT_LIB) $(LIBS)

# Release archive for Aminet: build/dist/AAText.lha + AAText.readme
# (docs are UTF-8 in the repository; the Turkish one is converted to
# ISO-8859-9, the usual Turkish character set on the Amiga)
DISTDIR := build/dist

.PHONY: dist
dist:
	$(MAKE) CPU=68020 DEBUG=0
	$(MAKE) CPU=68060 DEBUG=0
	$(MAKE) CPU=68020 DEBUG=1
	rm -rf $(DISTDIR)
	mkdir -p $(DISTDIR)/AAText
	cp build/68020/AAText $(DISTDIR)/AAText/AAText
	cp build/68060/AAText $(DISTDIR)/AAText/AAText.060
	cp build/68020-debug/AAText $(DISTDIR)/AAText/AAText.debug
	cp docs/AAText_EN.txt docs/AAText.prefs.example $(DISTDIR)/AAText/
	iconv -f UTF-8 -t ISO-8859-9 docs/AAText_TR.txt > $(DISTDIR)/AAText/AAText_TR.txt
	cp docs/AAText.readme $(DISTDIR)/AAText.readme
	chmod 644 $(DISTDIR)/AAText/*.txt $(DISTDIR)/AAText/*.example $(DISTDIR)/AAText.readme
	chmod 755 $(DISTDIR)/AAText/AAText $(DISTDIR)/AAText/AAText.060 $(DISTDIR)/AAText/AAText.debug
	cd $(DISTDIR) && lha ao5 AAText.lha AAText
	@ls -l $(DISTDIR)
