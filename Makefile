# AAText - antialiased Text() patch for AmigaOS 3.2
#
#   make            release build (68020+)
#   make DEBUG=1    debug build with serial kprintf output
#   make CPU=68060  build for 68060
#
# Normally invoked inside Docker via build.ps1.

PREFIX  ?= /opt/amiga/bin/m68k-amigaos-
CC      := $(PREFIX)gcc
AS      := $(PREFIX)gcc
STRIP   := $(PREFIX)strip

CPU     ?= 68020
DEBUG   ?= 0

BUILDDIR := build/$(CPU)$(if $(filter 1,$(DEBUG)),-debug,)
TARGET   := $(BUILDDIR)/AAText

CFLAGS  := -m$(CPU) -O2 -noixemul -fomit-frame-pointer \
           -Wall -Wextra -Wno-unused-parameter -Isrc
ASFLAGS := -m$(CPU) -noixemul
LDFLAGS := -m$(CPU) -noixemul

ifeq ($(DEBUG),1)
CFLAGS  += -DDEBUG
endif

SRCS_C := src/main.c src/patch.c src/render.c src/debug.c
SRCS_S := src/stub.s src/cgx.s
OBJS   := $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS_C)) \
          $(patsubst src/%.s,$(BUILDDIR)/%.o,$(SRCS_S))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
ifneq ($(DEBUG),1)
	$(STRIP) --strip-unneeded $@
endif
	@ls -l $@

$(BUILDDIR)/%.o: src/%.c src/*.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/%.o: src/%.s | $(BUILDDIR)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILDDIR):
	mkdir -p $@

clean:
	rm -rf build
