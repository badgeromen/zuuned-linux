# libzune — Makefile
#
# Builds libzune.so (Linux) or libzune.dylib (macOS) + libzune.a
#
# Native PTP/MTP/MTPZ stack — no vendored libmtp dependency.
#
# Dependencies:
#   - libgcrypt (for MTPZ authentication)
#   - macOS: IOKit framework (for DEXT IOUserClient)
#   - Linux: libusb-1.0
#   - ffmpeg CLI (runtime, for transcode/thumbnail functions)

# ---- OS detection ----

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  SHARED_EXT := dylib
  SHARED_FLAG := -dynamiclib
  INSTALL_NAME := -install_name @rpath/libzune.$(SHARED_EXT)
else
  SHARED_EXT := so
  SHARED_FLAG := -shared
  INSTALL_NAME :=
endif

# ---- Paths ----

PREFIX   ?= /usr/local
SRCDIR   := src
INCDIR   := include
OBJDIR   := obj
LIBDIR   := ../lib

# ---- Compiler/linker ----

CC       ?= cc
AR       ?= ar

CFLAGS   += -std=c99 -O2 -Wall -Wextra -Wpedantic -fPIC \
            -I$(INCDIR) -I$(SRCDIR) \
            -D_GNU_SOURCE

# LIBS = the actual libraries to link (shared with the CLI harness).
# LDFLAGS adds the shared-object flags on top; LDFLAGS_EXE is LIBS only,
# so `make zunetool` links an executable instead of a .so.
LDFLAGS  += $(SHARED_FLAG) $(INSTALL_NAME)

# ---- Source files (always compiled) ----

SRCS     := $(SRCDIR)/device.c \
            $(SRCDIR)/track.c \
            $(SRCDIR)/video.c \
            $(SRCDIR)/photo.c \
            $(SRCDIR)/album.c \
            $(SRCDIR)/playlist.c \
            $(SRCDIR)/thumbnail.c \
            $(SRCDIR)/zmdb.c \
            $(SRCDIR)/transcode.c \
            $(SRCDIR)/util.c \
            $(SRCDIR)/usb_recovery.c \
            $(SRCDIR)/finalize.c \
            $(SRCDIR)/search.c \
            $(SRCDIR)/ptp.c \
            $(SRCDIR)/mtp.c \
            $(SRCDIR)/mtpz.c

# ---- Platform-specific USB backend + dependencies ----

ifeq ($(UNAME),Darwin)
  # macOS: DEXT IOUserClient backend, no libusb
  SRCS    += $(SRCDIR)/driverkit_usb.c
  CFLAGS  += -I/opt/homebrew/opt/libgcrypt/include \
             -I/opt/homebrew/opt/libgpg-error/include \
             -DUSE_LIBAV \
             -Ivendor/ffmpeg-include
  LIBS += -L/opt/homebrew/opt/libgcrypt/lib -lgcrypt \
             -framework IOKit -framework CoreFoundation
else
  # Linux: libusb-1.0 backend + in-process libav probe (no ffprobe
  # subprocess — same USE_LIBAV path the macOS build uses)
  SRCS    += $(SRCDIR)/usb_libusb.c
  CFLAGS  += $(shell pkg-config --cflags libusb-1.0 2>/dev/null) \
             -DUSE_LIBAV \
             $(shell pkg-config --cflags libavformat libavutil 2>/dev/null)
  LIBS += -lgcrypt \
             $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo -lusb-1.0) \
             $(shell pkg-config --libs libavformat libavutil 2>/dev/null || echo -lavformat -lavutil)
endif

# Fold LIBS into both link lines.
LDFLAGS     += $(LIBS)
LDFLAGS_EXE := $(LIBS)

# ---- Object files ----

OBJS     := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# ---- Targets ----

SHARED   := libzune.$(SHARED_EXT)
STATIC   := libzune.a

.PHONY: all clean install uninstall zunetool

all: $(STATIC) $(SHARED)

# ---- CLI test harness (Linux: real device via libusb; macOS: links but
#      needs the DEXT running to find a device) ----
zunetool: $(STATIC)
	$(CC) $(CFLAGS) tools/zunetool.c $(STATIC) $(LDFLAGS_EXE) -o zunetool
	@echo "built ./zunetool — try: sudo ./zunetool info"

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/zune.h $(SRCDIR)/zune_internal.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SHARED): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(STATIC): $(OBJS)
	$(AR) rcs $@ $^

# ---- Install ----

install: $(SHARED) $(STATIC)
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 $(SHARED)          $(DESTDIR)$(PREFIX)/lib/
	install -m 644 $(STATIC)          $(DESTDIR)$(PREFIX)/lib/
	install -m 644 $(INCDIR)/zune.h   $(DESTDIR)$(PREFIX)/include/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(SHARED)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(STATIC)
	rm -f $(DESTDIR)$(PREFIX)/include/zune.h

# ---- Clean ----

clean:
	rm -rf $(OBJDIR) $(SHARED) $(STATIC)
