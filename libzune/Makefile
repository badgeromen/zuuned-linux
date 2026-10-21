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

# ---- Version (from the macros in include/zune.h) ----

VERSION_PART = $(shell sed -n 's/^\#define LIBZUNE_VERSION_$(1) \([0-9][0-9]*\)$$/\1/p' include/zune.h)
VERSION_MAJOR := $(call VERSION_PART,MAJOR)
VERSION_MINOR := $(call VERSION_PART,MINOR)
VERSION       := $(VERSION_MAJOR).$(VERSION_MINOR).$(call VERSION_PART,PATCH)

# ---- OS detection ----

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  SHARED_EXT := dylib
  SHARED_FLAG := -dynamiclib
  # Zuuned links against @rpath/libzune.dylib; keep that install name.
  INSTALL_NAME := -install_name @rpath/libzune.$(SHARED_EXT) \
                  -current_version $(VERSION) \
                  -compatibility_version $(VERSION_MAJOR).$(VERSION_MINOR).0
  SHARED_REAL := libzune.$(SHARED_EXT)
else
  SHARED_EXT := so
  SHARED_FLAG := -shared
  # libzune.so.0.3.0, SONAME libzune.so.0, development link libzune.so.
  SONAME := libzune.so.$(VERSION_MAJOR)
  INSTALL_NAME := -Wl,-soname,$(SONAME)
  SHARED_REAL := libzune.so.$(VERSION)
  # Link only what the objects use, and refuse undefined symbols: with
  # --as-needed (Debian and Ubuntu default) a library named before the
  # objects is dropped, so a misordered link fails here instead of producing
  # a libzune.so that cannot be loaded.
  SHARED_CHECKS := -Wl,--as-needed -Wl,-z,defs
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

# override: flags given on the command line (CI uses make CFLAGS+=-Werror)
# extend these instead of replacing the include paths and warnings.
override CFLAGS += -std=c99 -O2 -Wall -Wextra -Wpedantic -fPIC -pthread \
            -fvisibility=hidden \
            -I$(INCDIR) -I$(SRCDIR) \
            -D_GNU_SOURCE

# LIBS = the actual libraries to link (shared with the CLI harness).
# LDFLAGS adds the shared-object flags; LIBS always follows the objects on a
# link line. LDFLAGS_EXE is LIBS only, so `make zunetool` links an
# executable instead of a .so.
override LDFLAGS += $(SHARED_FLAG) $(INSTALL_NAME) $(SHARED_CHECKS)

# ---- Source files (always compiled) ----

SRCS     := $(SRCDIR)/device.c \
            $(SRCDIR)/devinfo.c \
            $(SRCDIR)/hd_artwork.c \
            $(SRCDIR)/track.c \
            $(SRCDIR)/video.c \
            $(SRCDIR)/photo.c \
            $(SRCDIR)/upload.c \
            $(SRCDIR)/album.c \
            $(SRCDIR)/playlist.c \
            $(SRCDIR)/podcast.c \
            $(SRCDIR)/audiobook.c \
            $(SRCDIR)/spoken_inventory.c \
            $(SRCDIR)/thumbnail.c \
            $(SRCDIR)/zmdb.c \
            $(SRCDIR)/zmdb_io.c \
            $(SRCDIR)/podcast_state.c \
            $(SRCDIR)/transcode.c \
            $(SRCDIR)/util.c \
            $(SRCDIR)/usb_recovery.c \
            $(SRCDIR)/finalize.c \
            $(SRCDIR)/search.c \
            $(SRCDIR)/wiretap.c \
            $(SRCDIR)/ptp.c \
            $(SRCDIR)/mtp.c \
            $(SRCDIR)/mtpz.c

# ---- Platform-specific USB backend + dependencies ----

ifeq ($(UNAME),Darwin)
  # macOS: DEXT IOUserClient backend, no libusb
  SRCS    += $(SRCDIR)/driverkit_usb.c
  override CFLAGS += -I/opt/homebrew/opt/libgcrypt/include \
             -I/opt/homebrew/opt/libgpg-error/include \
             -DUSE_LIBAV \
             $(shell pkg-config --cflags libavcodec libavformat libavutil 2>/dev/null || echo -I/opt/homebrew/opt/ffmpeg/include) \
             -Ivendor/ffmpeg-include
  LIBS += $(shell pkg-config --libs libavcodec libavformat libavutil 2>/dev/null || echo -L/opt/homebrew/opt/ffmpeg/lib -lavcodec -lavformat -lavutil) \
             -L/opt/homebrew/opt/libgcrypt/lib -lgcrypt \
             -framework IOKit -framework CoreFoundation
else
  # Linux: libusb-1.0 backend + in-process libav probe (no ffprobe
  # subprocess — same USE_LIBAV path the macOS build uses)
  SRCS    += $(SRCDIR)/usb_libusb.c
  override CFLAGS += $(shell pkg-config --cflags libusb-1.0 2>/dev/null) \
             -DUSE_LIBAV \
             $(shell pkg-config --cflags libavformat libavutil 2>/dev/null)
  LIBS += -lavcodec -lgcrypt \
             $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo -lusb-1.0) \
             $(shell pkg-config --libs libavformat libavutil 2>/dev/null || echo -lavformat -lavutil)
endif
LIBS += -pthread

LDFLAGS_EXE := $(LIBS)

# ---- Object files ----

OBJS     := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# ---- Targets ----

SHARED   := libzune.$(SHARED_EXT)
STATIC   := libzune.a
PKGCONFIG := libzune.pc

.PHONY: all clean install uninstall zunetool examples

all: $(STATIC) $(SHARED) $(PKGCONFIG)

# ---- CLI test harness (Linux: real device via libusb; macOS: links but
#      needs the DEXT running to find a device) ----
zunetool: $(STATIC)
	$(CC) $(CFLAGS) tools/zunetool.c $(STATIC) $(LDFLAGS_EXE) -lavcodec -o zunetool
	@echo "built ./zunetool — try: sudo ./zunetool info"

# Standalone public-API examples. Building never connects to hardware.
EXAMPLES := device_info spoken_inventory podcast_send audiobook_send hd_artwork
EXAMPLE_BINS := $(addprefix examples/bin/,$(EXAMPLES))
examples: $(EXAMPLE_BINS)

examples/bin:
	mkdir -p $@

examples/bin/%: examples/%.c examples/common.h $(STATIC) | examples/bin
	$(CC) $(CFLAGS) $< $(STATIC) $(LDFLAGS_EXE) -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/zune.h $(SRCDIR)/zune_internal.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/hd_artwork.o: $(wildcard $(SRCDIR)/hd/*.h)

$(OBJDIR)/mtp.o: $(SRCDIR)/mtp.h $(SRCDIR)/mtp_text.h
$(OBJDIR)/podcast.o $(OBJDIR)/spoken_inventory.o: $(SRCDIR)/mtp.h $(SRCDIR)/mtp_text.h

ifeq ($(UNAME),Darwin)
$(SHARED): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
else
$(SHARED_REAL): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(SONAME): $(SHARED_REAL)
	ln -sf $(SHARED_REAL) $@

$(SHARED): $(SONAME)
	ln -sf $(SONAME) $@
endif

# pkg-config metadata for prefix $(1), written to $(2). Static consumers need
# the private libraries too. `make install` writes it again for the install
# prefix, so a PREFIX given only to `make install` is never stale.
write_pc = printf '%s\n' \
	  'prefix=$(1)' \
	  'exec_prefix=$${prefix}' \
	  'libdir=$${exec_prefix}/lib' \
	  'includedir=$${prefix}/include' \
	  '' \
	  'Name: libzune' \
	  'Description: Zune media player management over PTP/MTP/MTPZ' \
	  'Version: $(VERSION)' \
	  'Libs: -L$${libdir} -lzune' \
	  'Libs.private: $(strip $(LIBS))' \
	  'Cflags: -I$${includedir}' > $(2)

$(PKGCONFIG): $(INCDIR)/zune.h Makefile
	@$(call write_pc,$(PREFIX),$@)

$(STATIC): $(OBJS)
	$(AR) rcs $@ $^

# ---- Install ----

install: $(SHARED) $(STATIC)
	install -d $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 755 $(SHARED_REAL)     $(DESTDIR)$(PREFIX)/lib/
ifneq ($(UNAME),Darwin)
	ln -sf $(SHARED_REAL) $(DESTDIR)$(PREFIX)/lib/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(PREFIX)/lib/$(SHARED)
endif
	install -m 644 $(STATIC)          $(DESTDIR)$(PREFIX)/lib/
	$(call write_pc,$(PREFIX),$(DESTDIR)$(PREFIX)/lib/pkgconfig/$(PKGCONFIG))
	chmod 644 $(DESTDIR)$(PREFIX)/lib/pkgconfig/$(PKGCONFIG)
	install -m 644 $(INCDIR)/zune.h   $(DESTDIR)$(PREFIX)/include/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(SHARED)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(SHARED_REAL)
ifneq ($(UNAME),Darwin)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(SONAME)
endif
	rm -f $(DESTDIR)$(PREFIX)/lib/$(STATIC)
	rm -f $(DESTDIR)$(PREFIX)/lib/pkgconfig/$(PKGCONFIG)
	rm -f $(DESTDIR)$(PREFIX)/include/zune.h

# ---- Clean ----

clean:
	rm -rf $(OBJDIR) $(SHARED) $(SHARED_REAL) $(SONAME) $(STATIC) $(PKGCONFIG) examples/bin
