# AAText - antialiased Text() patch for AmigaOS 3.2
#
#   make            release build (68020+)
#   make DEBUG=1    debug build with serial kprintf output
#   make CPU=68060  build for 68060
#   make USE_AATEXTLIB=1   FreeType from aatext.library instead of built in
#                   (build/<cpu>-lib/AAText; headers in include/aatextlib)
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
USE_AATEXTLIB ?= 0

FT_VER  := 2.14.3
FT_DIR  := third_party/freetype-$(FT_VER)

BUILDDIR := build/$(CPU)$(if $(filter 1,$(DEBUG)),-debug,)$(if $(filter 1,$(USE_AATEXTLIB)),-lib,)
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

# FreeType in the program, or from aatext.library: then only AAText's
# memory functions (aa_ftsystem.c) are linked, the rest is in the library
ifeq ($(USE_AATEXTLIB),1)
CFLAGS  += -DAA_USE_AATEXTLIB -Iinclude/aatextlib
FT_LINK  = $(BUILDDIR)/ft/aa_ftsystem.o
else
FT_LINK  = $(FT_LIB)
endif

SRCS_C := src/main.c src/patch.c src/render.c src/metrics.c src/prefs.c \
          src/glyphs.c src/otag.c src/aaclient.c src/debug.c src/charsets.c src/fontfile.c
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
           $(FT_DIR)/src/autofit/autofit.c \
           $(FT_DIR)/src/cff/cff.c \
           $(FT_DIR)/src/psaux/psaux.c \
           $(FT_DIR)/src/pshinter/pshinter.c \
           $(FT_DIR)/src/psnames/psnames.c \
           src/ft/aa_ftsystem.c
FT_OBJS := $(addprefix $(BUILDDIR)/ft/,$(notdir $(FT_SRCS:.c=.o)))
FT_LIB  := $(BUILDDIR)/libft.a

vpath %.c $(sort $(dir $(FT_SRCS)))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) $(FT_LINK)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(FT_LINK) $(LIBS)
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
             $(BUILDDIR)/otag.o $(BUILDDIR)/debug.o $(BUILDDIR)/charsets.o $(BUILDDIR)/fontfile.o $(BUILDDIR)/fontscan.o \
             $(BUILDDIR)/otagfile.o $(BUILDDIR)/fontinfo.o $(BUILDDIR)/fontinstall.o \
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
	$(MAKE) CPU=68020 DEBUG=0 gui catalogs icons
	rm -rf $(DISTDIR)
	mkdir -p $(DISTDIR)/AAText
	cp build/68020/AAText $(DISTDIR)/AAText/AAText
	cp build/68020/AATextPrefs $(ICON) $(DISTDIR)/AAText/
	# catalog directory "türkçe": UTF-8 here, lha stores it as Latin-1,
	# where ü and ç have the same codes as in ISO-8859-9 on the Amiga
	d="$(DISTDIR)/AAText/Catalogs/türkçe"; \
		mkdir -p "$$d" && cp $(CATALOG) "$$d/" && chmod 644 "$$d/aatextprefs.catalog"
	chmod 644 $(DISTDIR)/AAText/AATextPrefs.info
	cp build/68060/AAText $(DISTDIR)/AAText/AAText.060
	cp build/68020-debug/AAText $(DISTDIR)/AAText/AAText.debug
	cp docs/AAText_EN.txt docs/AAText.prefs.example $(DISTDIR)/AAText/
	cp LICENSE $(DISTDIR)/AAText/LICENSE.txt
	cp LICENSE.APL $(DISTDIR)/AAText/LICENSE.APL.txt
	cp docs/ftcodepage.latin5 $(DISTDIR)/AAText/ftcodepage.latin5
	chmod 644 $(DISTDIR)/AAText/ftcodepage.latin5
	iconv -f UTF-8 -t ISO-8859-9 docs/AAText_TR.txt > $(DISTDIR)/AAText/AAText_TR.txt
	cp docs/AAText.readme $(DISTDIR)/AAText.readme
	chmod 644 $(DISTDIR)/AAText/*.txt $(DISTDIR)/AAText/*.example $(DISTDIR)/AAText.readme
	chmod 755 $(DISTDIR)/AAText/AAText $(DISTDIR)/AAText/AAText.060 $(DISTDIR)/AAText/AAText.debug \
		$(DISTDIR)/AAText/AATextPrefs
	cd $(DISTDIR) && lha ao5 --system-kanji-code=utf8 --archive-kanji-code=latin1 \
		AAText.lha AAText
	@ls -l $(DISTDIR)

# Preferences program (ReAction); objects in their own directory because
# both programs have a main.c
GUI_TARGET := $(BUILDDIR)/AATextPrefs
GUI_SRCS   := src/prefsgui/main.c src/prefsgui/strings.c src/prefs.c \
              src/prefswrite.c src/aaclient.c src/charsets.c src/otag.c src/fontfile.c src/fontscan.c \
              src/otagfile.c
GUI_OBJS   := $(patsubst src/%.c,$(BUILDDIR)/gui/%.o,$(GUI_SRCS))

.PHONY: gui
gui: $(GUI_TARGET)

$(GUI_TARGET): $(GUI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/gui/%.o: src/%.c src/*.h src/prefsgui/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-pointer-sign -c -o $@ $<

# Turkish catalog for AATextPrefs
CATALOG := build/catalogs/aatextprefs.catalog

.PHONY: catalogs
catalogs: $(CATALOG)

$(CATALOG): catalogs/turkish.ct src/prefsgui/strings.h tools/mkcatalog.pl
	@mkdir -p $(dir $@)
	perl tools/mkcatalog.pl src/prefsgui/strings.h catalogs/turkish.ct $@

# GlowIcon for AATextPrefs, from the PNG art in icons/
ICON := build/icons/AATextPrefs.info

.PHONY: icons
icons: $(ICON)

$(ICON): icons/AATextPrefs.png icons/AATextPrefs_sel.png tools/mkicon.py
	@mkdir -p $(dir $@)
	python3 -I tools/mkicon.py tool icons/AATextPrefs.png \
		icons/AATextPrefs_sel.png --stack 16384 -o $@

# Font installer; FreeType from aatext.library
MGR_TARGET := $(BUILDDIR)/AATextManager
MGR_SRCS   := src/manager/main.c src/manager/gui.c src/manager/strings.c src/prefs.c src/debug.c src/fontinstall.c src/fontinfo.c \
              src/otagfile.c src/otag.c src/charsets.c src/fontscan.c src/fontfile.c
MGR_OBJS   := $(patsubst src/%.c,$(BUILDDIR)/mgr/%.o,$(MGR_SRCS))

.PHONY: manager
manager: $(MGR_TARGET)

$(MGR_TARGET): $(MGR_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/mgr/%.o: src/%.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DAA_USE_AATEXTLIB -Iinclude/aatextlib -Wno-pointer-sign -c -o $@ $<

# Turkish catalog for AATextManager
MGR_CATALOG := build/catalogs/aatextmanager.catalog
catalogs: $(MGR_CATALOG)

$(MGR_CATALOG): catalogs/manager-turkish.ct src/manager/strings.h tools/mkcatalog.pl
	@mkdir -p $(dir $@)
	perl tools/mkcatalog.pl src/manager/strings.h catalogs/manager-turkish.ct $@
