# Arctian Emulator — .wlf calistirici (Windows / Linux-X11 / headless)
#
# Piksel sadakati icin OS kaynaklari KAYNAK DEGISTEN derlenir:
#   desktop/wlf_host.c  desktop/wlf_vm.c  desktop/port/lv_port.c
#   desktop/theme/adl_wolf.c  desktop/sig.c  lib/png.c  + ayni LVGL + Inter.ttf
# Emulatorin kendi katmani: sdk/emu/*.c (+ shim/dsk.h).
#
# Hedefler:
#   make            -> headless + (X11 basliklari varsa) pencereli emulator
#   make headless   -> build/arctian-emu-headless
#   make x11        -> build/arctian-emu
#   make test       -> ornek uygulamayi PNG ureterek calistirir
#   make clean
ROOT     := ../..
BUILD    := $(ROOT)/build/emu
BIN      := $(BUILD)/arctian-emu
BIN_HEAD := $(BUILD)/arctian-emu-headless

CC       ?= gcc
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -DNDEBUG -w -fno-strict-aliasing -MMD -MP
# shim/dsk.h ONECE gelir: OS kaynaklari masaustu dsk.h yerine bunu gorur.
# Makefile sdk/emu icinden calistigi icin tum yollar ROOT-onekli.
INCLUDES := -I$(ROOT)/sdk/emu/shim -I$(ROOT)/sdk/emu \
            -I$(ROOT)/include/arctian -I$(ROOT)/desktop -I$(ROOT)/desktop/shell \
            -I$(ROOT)/desktop/ui-kit -I$(ROOT)/desktop/port -I$(ROOT)/desktop/theme \
            -I$(ROOT)/install -I$(ROOT)/lib \
            -I$(ROOT)/third_party -I$(ROOT)/third_party/lvgl \
            -DLV_CONF_INCLUDE_SIMPLE
LDLIBS   := -lm

# ---- LVGL: OS Makefile'inin kaynak listesiyle BIREBIR ayni ----
LVGL_C := $(shell cd $(ROOT) && find third_party/lvgl/src -name '*.c' \
           ! -path '*/osal/lv_linux.c' ! -path '*/osal/lv_pthread.c' \
           ! -path '*/osal/lv_windows.c' ! -path '*/osal/lv_sdl2.c' \
           ! -path '*/osal/lv_freertos.c' ! -path '*/osal/lv_rtthread.c' \
           ! -path '*/osal/lv_cmsis_rtos2.c' ! -path '*/osal/lv_mqx.c' \
           ! -path '*/drivers/*' \
           ! -path '*/draw/nanovg/*' ! -path '*/draw/vg_lite/*' \
           ! -path '*/draw/nema_gfx/*' ! -path '*/draw/opengles/*' \
           ! -path '*/draw/sdl/*' ! -path '*/draw/dma2d/*' \
           ! -path '*/draw/nxp/*' ! -path '*/draw/renesas/*' \
           ! -path '*/draw/eve/*' ! -path '*/draw/sifli/*' \
           ! -path '*/debugging/vg_lite_tvg/*' ! -path '*/debugging/profiler/*' \
           ! -path '*/debugging/sysmon/*' ! -path '*/debugging/test/*' \
           ! -path '*/debugging/monkey/*' \
           ! -path '*/libs/*' \
           ! -path '*/fs/lv_fs_stdio.c' ! -path '*/fs/lv_fs_posix.c' \
           ! -path '*/fs/lv_fs_win32.c' ! -path '*/fs/lv_fs_uefi.c' \
           ! -path '*/fs/lv_fs_frogfs.c' ! -path '*/fs/lv_fs_littlefs.c' \
           ! -path '*/fs/lv_fs_fatfs.c' \
           ! -path '*/font/freetype/*' 2>/dev/null)

# ---- OS'tan dogrudan derlenen dosyalar ----
OS_C := desktop/wlf_host.c desktop/wlf_vm.c desktop/port/lv_port.c \
        desktop/theme/adl_wolf.c desktop/sig.c lib/png.c

# ---- emulatorin kendi kaynaklari ----
EMU_C := sdk/emu/emu_main.c sdk/emu/emu_shim.c sdk/emu/emu_pkg.c \
         sdk/emu/emu_native.c sdk/emu/emu_png.c sdk/emu/emu_bridge.c \
         sdk/emu/emu_time.c

LVGL_OBJS := $(addprefix $(BUILD)/,$(LVGL_C:.c=.o))
OS_OBJS   := $(addprefix $(BUILD)/,$(OS_C:.c=.o))
EMU_OBJS  := $(addprefix $(BUILD)/,$(EMU_C:.c=.o))
FONT_C    := $(BUILD)/desktop/assets/font_data.c
FONT_OBJ  := $(FONT_C:.c=.o)
PLAT_HEAD := $(BUILD)/sdk/emu/plat_headless.o
PLAT_X11  := $(BUILD)/sdk/emu/plat_x11.o

X11_OK := $(shell test -f /usr/include/X11/Xlib.h && echo yes)

.PHONY: all headless x11 test clean
all: headless $(if $(X11_OK),x11,)
	@echo "emu hazir: $(BIN_HEAD)$(if $(X11_OK), $(BIN))"

headless: $(BIN_HEAD)
x11: $(BIN)

$(BIN_HEAD): $(LVGL_OBJS) $(OS_OBJS) $(EMU_OBJS) $(FONT_OBJ) $(PLAT_HEAD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN): $(LVGL_OBJS) $(OS_OBJS) $(EMU_OBJS) $(FONT_OBJ) $(PLAT_X11)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lX11

# font gomulur (OS ile ayni Inter.ttf, ayni arac)
$(FONT_C): $(ROOT)/design/font/Inter.ttf $(ROOT)/tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 $(ROOT)/tools/bin2c.py $< $@ font_ttf

$(BUILD)/sdk/emu/plat_headless.o: $(ROOT)/sdk/emu/plat_headless.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -DEMU_PLAT_HEADLESS -c -o $@ $<

$(BUILD)/sdk/emu/plat_x11.o: $(ROOT)/sdk/emu/plat_x11.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -DEMU_PLAT_X11 -c -o $@ $<

# kaynak agaci ROOT'e gore; derleyici sdk/emu icinden calisir
$(BUILD)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

test: $(BIN_HEAD)
	$(BIN_HEAD) $(ROOT)/sdk/examples/merhaba/merhaba.wlf --headless \
	    --shot $(BUILD)/merhaba.png --frames 60

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
