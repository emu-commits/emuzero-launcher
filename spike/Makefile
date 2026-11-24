# SPDX-License-Identifier: MIT
# Cross-build the renderer spikes with the NimbleZero Buildroot toolchain.
BR      ?= $(HOME)/cardputerzero-os/NimbleZero/out/compat
CC      := $(BR)/host/bin/aarch64-linux-gcc
AR      := $(BR)/host/bin/aarch64-linux-ar
STRIP   := $(BR)/host/bin/aarch64-linux-strip
SYSROOT := $(BR)/host/aarch64-buildroot-linux-gnu/sysroot
LVGL    := ../third_party/lvgl
OUT     := out
CFLAGS  := -O2 -Wall -I$(SYSROOT)/usr/include/freetype2
LVCFLAGS:= $(CFLAGS) -DLV_CONF_INCLUDE_SIMPLE -I. -I../third_party -w

LV_SRC  := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ  := $(patsubst $(LVGL)/src/%.c,$(OUT)/lv/%.o,$(LV_SRC))

all: $(OUT)/spike-hand $(OUT)/spike-lvgl $(OUT)/tstart

$(OUT)/lv/%.o: $(LVGL)/src/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(LVCFLAGS) -c $< -o $@

$(OUT)/liblvgl.a: $(LV_OBJ)
	$(AR) rcs $@ $^

$(OUT)/spike-lvgl: spike-lvgl.c common.h $(OUT)/liblvgl.a
	$(CC) $(LVCFLAGS) $< $(OUT)/liblvgl.a -lfreetype -lpng16 -lz -lm -o $@
	$(STRIP) $@

$(OUT)/spike-hand: spike-hand.c common.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -lfreetype -lpng16 -lz -lm -o $@
	$(STRIP) $@

$(OUT)/tstart: tstart.c
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@
	$(STRIP) $@

clean:
	rm -rf $(OUT)
