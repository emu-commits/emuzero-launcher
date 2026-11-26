# SPDX-License-Identifier: MIT
# emuZero launcher. One source tree, three builds:
#
#   make device     cross-build for the CardputerZero with the NimbleZero Buildroot toolchain (BR=...)
#   make desktop    a window on this PC (needs SDL2 development files)
#   make headless   draws into memory, writes PNGs; for checks without a screen
#
# Buildroot calls `make PLATFORM=fbdev` with its own CC, CFLAGS and PKG_CONFIG.

PLATFORM   ?= fbdev
VERSION    ?= $(shell git describe --always --dirty 2>/dev/null || echo dev)
LVGL_PARENT?= third_party
LVGL       := $(LVGL_PARENT)/lvgl
OUT        ?= out/$(PLATFORM)
CC         ?= cc
AR         ?= ar
PKG_CONFIG ?= pkg-config
CFLAGS     ?= -O2
DESTDIR    ?=
PREFIX     ?= /usr

ifeq ($(PLATFORM),fbdev)
PLAT_ID  := EMUZERO_FBDEV
PLAT_LIBS:=
else ifeq ($(PLATFORM),sdl)
PLAT_ID  := EMUZERO_SDL
# pkg-config first; sdl2-config when that pkg-config does not see the system's SDL2 (Homebrew's)
SDL2_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   ?= $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)
ifeq ($(strip $(SDL2_LIBS)),)
$(error SDL2 development files not found: install them (Ubuntu: sudo apt install libsdl2-dev; Homebrew: brew install sdl2), or pass SDL2_CFLAGS and SDL2_LIBS)
endif
PLAT_CFLAGS := $(SDL2_CFLAGS)
PLAT_LIBS   := $(SDL2_LIBS)
else ifeq ($(PLATFORM),headless)
PLAT_ID  := EMUZERO_HEADLESS
PLAT_LIBS:=
else
$(error PLATFORM must be fbdev, sdl or headless)
endif

DEPS_CFLAGS := $(shell $(PKG_CONFIG) --cflags freetype2 libpng)
DEPS_LIBS   := $(shell $(PKG_CONFIG) --libs freetype2 libpng) -lm
ALL_CFLAGS  := $(CFLAGS) -DEMUZERO_PLATFORM=$(PLAT_ID) -DLV_CONF_INCLUDE_SIMPLE -Isrc -I$(LVGL_PARENT) \
               $(DEPS_CFLAGS) $(PLAT_CFLAGS)

SRC := src/main.c src/ui.c src/statusbar.c src/status.c src/appinfo.c src/sys.c src/apps.c src/icons.c src/theme.c src/themepage.c \
       src/layout_carousel.c src/layout_grid.c src/paths.c \
       src/plat_$(PLATFORM).c
OBJ := $(patsubst src/%.c,$(OUT)/%.o,$(SRC))
LV_SRC := $(shell find $(LVGL)/src -name '*.c' 2>/dev/null)
LV_OBJ := $(patsubst $(LVGL)/src/%.c,$(OUT)/lv/%.o,$(LV_SRC))
BIN := $(OUT)/emuzero-launcher

# emuZero Notes (apps/notes): its own program, sharing LVGL, the themes and the font paths
NOTES_SRC := $(wildcard apps/notes/*.c)
NOTES_SRC := $(filter-out apps/notes/nplat_%.c,$(NOTES_SRC)) apps/notes/nplat_$(PLATFORM).c
NOTES_SHARED := src/theme.c src/paths.c src/status.c src/sys.c
NOTES_OBJ := $(patsubst apps/notes/%.c,$(OUT)/notes/%.o,$(NOTES_SRC)) $(patsubst src/%.c,$(OUT)/notes/shared/%.o,$(NOTES_SHARED))
NOTES_BIN := $(OUT)/emuzero-notes

# emuZero Wiki (apps/wiki): its own program, built on Notes' modules; SQLite and zlib from the system
comma := ,
WIKI_SRC := $(wildcard apps/wiki/*.c)
WIKI_FROM_NOTES := nplat_$(PLATFORM) keyspec fonts draw textbuf doc md view clip store search
WIKI_OBJ := $(patsubst apps/wiki/%.c,$(OUT)/wiki/%.o,$(WIKI_SRC)) $(patsubst %,$(OUT)/notes/%.o,$(WIKI_FROM_NOTES)) \
            $(patsubst src/%.c,$(OUT)/notes/shared/%.o,$(NOTES_SHARED)) $(OUT)/wiki/tjpgd.o
WIKI_BIN := $(OUT)/emuzero-wiki
SQLITE_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sqlite3 zlib)
SQLITE_LIBS   ?= $(shell $(PKG_CONFIG) --libs sqlite3 zlib)
ifneq ($(PLATFORM),fbdev)
# a desktop build may find SQLite or zlib outside the usual places (Homebrew): find them again at run time
SQLITE_RPATH := $(patsubst -L%,-Wl$(comma)-rpath$(comma)%,$(filter -L%,$(SQLITE_LIBS)))
endif

all: $(BIN) $(NOTES_BIN) $(WIKI_BIN)

$(LVGL)/lvgl.h:
	@echo "LVGL is missing: run third_party/fetch.sh (or pass LVGL_PARENT=dir-containing-lvgl)"; exit 1

$(OUT)/lv/%.o: $(LVGL)/src/%.c src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	@$(CC) $(ALL_CFLAGS) -w -c $< -o $@

$(OUT)/liblvgl.a: $(LV_OBJ)
	@echo "  AR $@"; $(AR) rcs $@ $^

$(OUT)/main.o: VFLAGS := -DEMUZERO_VERSION='"$(VERSION)"'
$(OUT)/%.o: src/%.c src/*.h src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(VFLAGS) -Wall -Wextra -c $< -o $@

$(BIN): $(OBJ) $(OUT)/liblvgl.a
	$(CC) $(LDFLAGS) $(OBJ) $(OUT)/liblvgl.a $(DEPS_LIBS) $(PLAT_LIBS) -o $@

$(OUT)/notes/main.o: VFLAGS := -DEMUZERO_VERSION='"$(VERSION)"'
$(OUT)/notes/%.o: apps/notes/%.c apps/notes/*.h src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -Iapps/notes $(VFLAGS) -Wall -Wextra -c $< -o $@
$(OUT)/notes/shared/%.o: src/%.c src/*.h src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -Wall -Wextra -c $< -o $@

$(NOTES_BIN): $(NOTES_OBJ) $(OUT)/liblvgl.a
	$(CC) $(LDFLAGS) $(NOTES_OBJ) $(OUT)/liblvgl.a $(DEPS_LIBS) $(PLAT_LIBS) -o $@

notes: $(NOTES_BIN)

$(OUT)/wiki/wmain.o: VFLAGS := -DEMUZERO_VERSION='"$(VERSION)"'
$(OUT)/wiki/image.o: VFLAGS := -DLV_USE_TJPGD=1
$(OUT)/wiki/%.o: apps/wiki/%.c apps/wiki/*.h apps/notes/notes.h src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(SQLITE_CFLAGS) -Iapps/wiki -Iapps/notes $(VFLAGS) -Wall -Wextra -c $< -o $@
# LVGL's JPEG decoder (TJpgDec), switched on for this program only: the launcher does not need it
$(OUT)/wiki/tjpgd.o: $(LVGL)/src/libs/tjpgd/tjpgd.c src/lv_conf.h | $(LVGL)/lvgl.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -DLV_USE_TJPGD=1 -w -c $< -o $@

$(WIKI_BIN): $(WIKI_OBJ) $(OUT)/liblvgl.a
	$(CC) $(LDFLAGS) $(WIKI_OBJ) $(OUT)/liblvgl.a $(DEPS_LIBS) $(SQLITE_LIBS) $(SQLITE_RPATH) $(PLAT_LIBS) -o $@

wiki: $(WIKI_BIN)

# The survival pack (in the image) is made from apps/wiki/content/survival and kept, built, as
# apps/wiki/packs/survival.wpack: an image build then needs no Python with sqlite3 (Buildroot's
# host Python has none). `make check` fails when the kept pack no longer matches its content;
# `make wiki-pack-update` makes it again. Wikipedia's pack is made by hand:
#   apps/wiki/tools/build-pack.py simplewiki wiki-slim.db simplewiki.wpack
WIKI_PACK := apps/wiki/packs/survival.wpack
WIKI_CONTENT := $(shell find apps/wiki/content/survival -type f 2>/dev/null)
$(OUT)/survival.wpack: apps/wiki/tools/build-pack.py $(WIKI_CONTENT)
	@mkdir -p $(dir $@)
	python3 apps/wiki/tools/build-pack.py survival apps/wiki/content/survival $@
	python3 apps/wiki/tools/build-pack.py check $@
wiki-pack: $(OUT)/survival.wpack
	@python3 apps/wiki/tools/build-pack.py same $(OUT)/survival.wpack $(WIKI_PACK) || \
		{ echo "$(WIKI_PACK) is out of date with apps/wiki/content: run make wiki-pack-update"; exit 1; }
wiki-pack-update: $(OUT)/survival.wpack
	@mkdir -p $(dir $(WIKI_PACK))
	cp $(OUT)/survival.wpack $(WIKI_PACK)

install: $(BIN) $(NOTES_BIN) $(WIKI_BIN)
	install -D -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/emuzero-launcher
	install -D -m 0755 $(NOTES_BIN) $(DESTDIR)$(PREFIX)/bin/emuzero-notes
	install -D -m 0755 apps/notes/nz-clip $(DESTDIR)$(PREFIX)/bin/nz-clip
	install -D -m 0644 apps/notes/guide.md $(DESTDIR)$(PREFIX)/share/emuzero/notes/guide.md
	install -D -m 0644 apps/notes/emuzero-notes.desktop $(DESTDIR)$(PREFIX)/share/applications/emuzero-notes.desktop
	install -D -m 0644 apps/calculator.desktop $(DESTDIR)$(PREFIX)/share/applications/calculator.desktop
	install -D -m 0755 $(WIKI_BIN) $(DESTDIR)$(PREFIX)/bin/emuzero-wiki
	install -D -m 0644 $(WIKI_PACK) $(DESTDIR)$(PREFIX)/share/emuzero/wiki/survival.wpack
	install -D -m 0644 apps/wiki/emuzero-wiki.desktop $(DESTDIR)$(PREFIX)/share/applications/emuzero-wiki.desktop

# ---- convenience targets ----
BR ?= $(HOME)/cardputerzero-os/NimbleZero/out/compat
device:
	$(MAKE) PLATFORM=fbdev CC=$(BR)/host/bin/aarch64-linux-gcc AR=$(BR)/host/bin/aarch64-linux-ar \
		PKG_CONFIG=$(BR)/host/bin/pkg-config CFLAGS="-O2"
	$(BR)/host/bin/aarch64-linux-strip out/fbdev/emuzero-launcher out/fbdev/emuzero-notes out/fbdev/emuzero-wiki
desktop:
	$(MAKE) PLATFORM=sdl CFLAGS="-O2 -g"
headless:
	$(MAKE) PLATFORM=headless CFLAGS="-O2 -g"

# headless checks against the apps in test/root: each layout after a key script, as PNGs
check: headless
	@test/check.sh out/headless/emuzero-launcher out/check
	@apps/notes/test/check.sh out/headless/emuzero-notes out/check-notes
	@$(MAKE) --no-print-directory PLATFORM=headless wiki-pack
	@apps/wiki/test/check.sh out/headless/emuzero-wiki $(WIKI_PACK) out/check-wiki

# the Notes app's unit tests (text buffer, undo, Markdown lines, names, saving): no LVGL needed
notes-test:
	@mkdir -p out/notes-test
	$(CC) -O1 -g -fsanitize=address,undefined -Wall -Wextra -Iapps/notes -Isrc -I$(LVGL_PARENT) -DLV_CONF_INCLUDE_SIMPLE \
		apps/notes/test/unit.c apps/notes/textbuf.c apps/notes/doc.c apps/notes/md.c apps/notes/store.c -o out/notes-test/unit
	out/notes-test/unit

clean:
	rm -rf out

.PHONY: all install device desktop headless check clean notes notes-test wiki wiki-pack wiki-pack-update
