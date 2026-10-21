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
          src/glyphs.c src/otag.c src/aaclient.c src/debug.c src/charsets.c src/fontfile.c \
          src/startup.c
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
             $(BUILDDIR)/startup.o $(BUILDDIR)/stub.o $(BUILDDIR)/prefswrite.o

.PHONY: test
test: $(BUILDDIR)/fttest

$(BUILDDIR)/fttest: tests/fttest.c $(TEST_OBJS) $(FT_LIB)
	$(CC) $(CFLAGS) -o $@ tests/fttest.c $(TEST_OBJS) $(FT_LIB) $(LIBS)

# lcdtest: subpixel (LCD) antialiasing test, see tests/lcdtest.c
.PHONY: lcdtest
lcdtest: $(BUILDDIR)/lcdtest

$(BUILDDIR)/lcdtest: tests/lcdtest.c $(FT_LIB) $(BUILDDIR)/cgx.o
	$(CC) $(CFLAGS) -o $@ tests/lcdtest.c $(BUILDDIR)/cgx.o $(FT_LIB) $(LIBS)

# MUI GUIs: MUI 3.8 headers from Aminet (tools/fetch-mui.sh), inline
# functions made with sfdc; MUI_NewObject() from src/mui/muistubs.c
MUI_INC   := third_party/mui/include
MUI_FLAGS := -I$(MUI_INC) -DNO_INLINE_STDARG

$(MUI_INC)/libraries/mui.h:
	@echo "MUI headers missing: run tools/fetch-mui.sh"; exit 1

# muitest: the MUI 3.8 classes the MUI GUIs use, see tests/muitest.c
.PHONY: muitest
muitest: $(BUILDDIR)/muitest

$(BUILDDIR)/muitest: tests/muitest.c src/mui/muistubs.c $(MUI_INC)/libraries/mui.h
	$(CC) $(CFLAGS) $(MUI_FLAGS) -Wno-pointer-sign -o $@ tests/muitest.c \
		src/mui/muistubs.c -lamiga

# lcdtest-ct: the same with FreeType's ClearType style LCD rendering and
# its filters (FT_CONFIG_OPTION_SUBPIXEL_RENDERING), in a FreeType of its
# own: AAText's FreeType stays as it is
CT_DIR  := $(BUILDDIR)/ftct
CT_OBJS := $(addprefix $(CT_DIR)/,$(notdir $(FT_SRCS:.c=.o)))
CT_LIB  := $(CT_DIR)/libft.a

.PHONY: lcdtest-ct
lcdtest-ct: $(BUILDDIR)/lcdtest-ct

$(CT_DIR)/%.o: %.c src/ft/aa_ftoption.h src/ft/aa_ftmodule.h
	@mkdir -p $(CT_DIR)
	$(CC) $(FT_CFLAGS) -DFT_CONFIG_OPTION_SUBPIXEL_RENDERING -c -o $@ $<

$(CT_LIB): $(CT_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

$(BUILDDIR)/lcdtest-ct: tests/lcdtest.c $(CT_LIB) $(BUILDDIR)/cgx.o
	$(CC) $(CFLAGS) -DFT_CONFIG_OPTION_SUBPIXEL_RENDERING -DLCD_CLEARTYPE \
		-o $@ tests/lcdtest.c $(BUILDDIR)/cgx.o $(CT_LIB) $(LIBS)

# Release archive for Aminet: build/dist/AAText.lha + AAText.readme
# (docs are UTF-8 in the repository; the Turkish one is converted to
# ISO-8859-9, the usual Turkish character set on the Amiga)
DISTDIR := build/dist
AATEXTLIB_PKG ?= third_party/aatext_lib

.PHONY: dist
dist:
	$(MAKE) CPU=68020 DEBUG=0
	$(MAKE) CPU=68060 DEBUG=0
	$(MAKE) CPU=68020 DEBUG=1
	$(MAKE) CPU=68020 DEBUG=0 gui manager catalogs icons
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
	# font installer and its library (copied to third_party/aatext_lib
	# from the aatext.library package: Libs/aatext.library,
	# LICENSE.txt, FTL.txt)
	cp build/68020/AATextManager $(MGR_ICON) $(DISTDIR)/AAText/
	chmod 644 $(DISTDIR)/AAText/AATextManager.info
	cp $(MGR_CATALOG) "$(DISTDIR)/AAText/Catalogs/türkçe/"
	chmod 644 "$(DISTDIR)/AAText/Catalogs/türkçe/aatextmanager.catalog"
	mkdir -p $(DISTDIR)/AAText/Libs
	cp $(AATEXTLIB_PKG)/aatext.library $(DISTDIR)/AAText/Libs/
	cp $(AATEXTLIB_PKG)/LICENSE.txt $(DISTDIR)/AAText/LICENSE.aatext.txt
	cp $(AATEXTLIB_PKG)/FTL.txt $(DISTDIR)/AAText/FTL.txt
	chmod 644 $(DISTDIR)/AAText/LICENSE.aatext.txt $(DISTDIR)/AAText/FTL.txt
	chmod 755 $(DISTDIR)/AAText/Libs/aatext.library $(DISTDIR)/AAText/AATextManager
	iconv -f UTF-8 -t ISO-8859-9 docs/AAText_TR.txt > $(DISTDIR)/AAText/AAText_TR.txt
	# installer script (Turkish strings and paths in ISO-8859-9) and its icon
	iconv -f UTF-8 -t ISO-8859-9 installer/Install-AAText > $(DISTDIR)/AAText/Install-AAText
	cp $(INST_ICON) $(DISTDIR)/AAText/Install-AAText.info
	chmod 644 $(DISTDIR)/AAText/Install-AAText $(DISTDIR)/AAText/Install-AAText.info
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
GUI_SRCS   := src/prefsgui/main.c src/prefsgui/core.c src/prefsgui/strings.c \
              src/prefs.c \
              src/prefswrite.c src/aaclient.c src/charsets.c \
              src/manager/preview.c
GUI_OBJS   := $(patsubst src/%.c,$(BUILDDIR)/gui/%.o,$(GUI_SRCS)) \
              $(BUILDDIR)/gui/cgx.o

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

# the mapping preview: AATextManager's, FreeType from aatext.library
$(BUILDDIR)/gui/manager/preview.o: src/manager/preview.c src/manager/preview.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DAA_USE_AATEXTLIB -Iinclude/aatextlib -Wno-pointer-sign -c -o $@ $<

$(BUILDDIR)/gui/cgx.o: src/cgx.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c -o $@ $<

# AATextPrefs for MUI 3.8: the same core, strings and preview
GUIMUI_TARGET := $(BUILDDIR)/AATextPrefs_MUI
GUIMUI_SRCS   := src/prefsgui_mui/main.c src/prefsgui/core.c \
                 src/prefsgui/strings.c src/prefs.c src/prefswrite.c \
                 src/aaclient.c src/charsets.c src/mui/muistubs.c
GUIMUI_OBJS   := $(patsubst src/%.c,$(BUILDDIR)/guimui/%.o,$(GUIMUI_SRCS)) \
                 $(BUILDDIR)/gui/manager/preview.o $(BUILDDIR)/gui/cgx.o

.PHONY: gui_mui
gui_mui: $(GUIMUI_TARGET)

$(GUIMUI_TARGET): $(GUIMUI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/guimui/%.o: src/%.c src/*.h src/prefsgui/*.h $(MUI_INC)/libraries/mui.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MUI_FLAGS) -Wno-pointer-sign -c -o $@ $<

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
MGR_SRCS   := src/manager/main.c src/manager/gui.c src/manager/core.c src/manager/settings.c src/manager/preview.c src/manager/update.c src/manager/strings.c src/prefs.c src/debug.c src/fontinstall.c src/fontinfo.c \
              src/otagfile.c src/otag.c src/charsets.c src/fontscan.c src/fontfile.c \
              src/aaclient.c
MGR_OBJS   := $(patsubst src/%.c,$(BUILDDIR)/mgr/%.o,$(MGR_SRCS)) $(BUILDDIR)/mgr/cgx.o

.PHONY: manager
manager: $(MGR_TARGET)

$(MGR_TARGET): $(MGR_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/mgr/%.o: src/%.c src/*.h src/manager/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DAA_USE_AATEXTLIB -Iinclude/aatextlib -Wno-pointer-sign -c -o $@ $<

# AATextManager for MUI 3.8: the same program with gui.c from manager_mui
MGRMUI_TARGET := $(BUILDDIR)/AATextManager_MUI
MGRMUI_OBJS   := $(filter-out $(BUILDDIR)/mgr/manager/gui.o,$(MGR_OBJS)) \
                 $(BUILDDIR)/mgrmui/manager_mui/gui.o \
                 $(BUILDDIR)/mgrmui/mui/muistubs.o

.PHONY: manager_mui
manager_mui: $(MGRMUI_TARGET)

$(MGRMUI_TARGET): $(MGRMUI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lamiga
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/mgrmui/%.o: src/%.c src/*.h src/manager/*.h $(MUI_INC)/libraries/mui.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(MUI_FLAGS) -Wno-pointer-sign -c -o $@ $<

# Turkish catalog for AATextManager
MGR_CATALOG := build/catalogs/aatextmanager.catalog
catalogs: $(MGR_CATALOG)

$(MGR_CATALOG): catalogs/manager-turkish.ct src/manager/strings.h tools/mkcatalog.pl
	@mkdir -p $(dir $@)
	perl tools/mkcatalog.pl src/manager/strings.h catalogs/manager-turkish.ct $@

# GlowIcon for AATextManager; the PNG art is drawn by
# tools/mkmanagericon.py from the AATextPrefs icon
MGR_ICON := build/icons/AATextManager.info
INST_ICON := build/icons/Install-AAText.info
icons: $(MGR_ICON) $(INST_ICON)

# project icon of the installer script, opened with the OS 3.2 Installer
$(INST_ICON): icons/AATextManager.png icons/AATextManager_sel.png tools/mkicon.py
	@mkdir -p $(dir $@)
	python3 -I tools/mkicon.py project icons/AATextManager.png 		icons/AATextManager_sel.png --deftool SYS:System/Installer 		--tooltype APPNAME=AAText --tooltype MINUSER=NOVICE 		--tooltype DEFUSER=AVERAGE -o $@

$(MGR_ICON): icons/AATextManager.png icons/AATextManager_sel.png tools/mkicon.py
	@mkdir -p $(dir $@)
	python3 -I tools/mkicon.py tool icons/AATextManager.png \
		icons/AATextManager_sel.png --stack 65536 -o $@

$(BUILDDIR)/mgr/cgx.o: src/cgx.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c -o $@ $<

# The update check uses AmiSSL 5 (headers from its SDK, fetched with
# tools/fetch-amissl.sh); amissl.library itself is opened at run time.
AMISSL_INC := third_party/amissl-sdk/AmiSSL/Developer/include

$(BUILDDIR)/mgr/manager/update.o: src/manager/update.c src/manager/update.h
	@test -f $(AMISSL_INC)/proto/amissl.h || \
		{ echo "AmiSSL SDK missing: run tools/fetch-amissl.sh"; exit 1; }
	@mkdir -p $(dir $@)
	$(CC) $(ARCHFLAGS) -O2 -fomit-frame-pointer -Wall -Isrc -I$(AMISSL_INC) \
		-c -o $@ $<
