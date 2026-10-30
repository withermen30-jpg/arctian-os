# Arctian - moduler isletim sistemi build

# ONEMLI: varsayilan hedefi AÇIKÇA sabitle.
# Asagidaki `-include $(find ... -name '*.d')` satiri `all:` hedefinden ÖNCE
# duruyor; GNU Make 4.x bu durumda .d dosyasindaki ilk hedefi (orn.
# build/obj-.../apps/asistan.o) varsayilan hedef sayiyor ve `make` / `make -j4`
# gibi hedefsiz çagrilar HICBIR SEY DERLEMEDEN "is up to date" deyip RC=0
# donuyordu -> build.bat "hazir" diyor, ISO/imaj YENILENMIYORDU.
.DEFAULT_GOAL := all

NASM    = nasm
CC      = gcc
LD      = ld
AR      = ar
OBJCOPY = objcopy
ZIG     ?= zig
RUSTC   ?= rustc
QEMU    = qemu-system-x86_64

EXTRA_CFLAGS ?=

DEBUG ?= 0
ifeq ($(DEBUG),1)
override EXTRA_CFLAGS += -DARCTIAN_DEBUG_BOOT -DARCTIAN_AUTOUSER
endif

SETUP_TEST ?= 0
ifeq ($(SETUP_TEST),1)
override EXTRA_CFLAGS += -DARCTIAN_SETUP_TEST
endif

T06_TEST ?= 0
ifeq ($(T06_TEST),1)
override EXTRA_CFLAGS += -DARCTIAN_06_TEST
endif

T07_TEST ?= 0
ifeq ($(T07_TEST),1)
override EXTRA_CFLAGS += -DARCTIAN_07_TEST
endif

VERSION_STR := $(shell python3 -c "import json;print(json.load(open('version.json'))['version'])" 2>/dev/null)
ifeq ($(strip $(VERSION_STR)),)
VERSION_STR := 0.0
endif
# ONEMLI: komut satirindan `make EXTRA_CFLAGS=...` verildiginde GNU make,
# Makefile icindeki `EXTRA_CFLAGS += ...` satirlarini YOK SAYAR. Bu yuzden
# surum bayragi ayri bir degiskende tutulur; aksi halde surum hep "0.0"
# (version.h yedegi) olarak gomuluyordu.
VERSION_CFLAGS := -DARCTIAN_VERSION_STR='"$(VERSION_STR)"'

# Derleme bayraklari degisince TUM nesneler gecersiz olur: DEBUG, T06_TEST,
# AUTOUSER gibi -D'ler farkli kod uretir. Onceden bayrak degisimi mevcut .o'lari
# yeniden derlemiyor, karisik nesnelerden (or. testler devre disi) imaj
# uretiliyordu.
#
# Cozum: nesneler bayrak kumesine ozel dizinde tutulur (build/obj-<hash>).
# Onceki surumde .flags degisince butun .o'lar siliniyordu; 9p dosya sisteminde
# (WSL -> Windows) yuzlerce dosyayi silmek make'i kilitliyordu. Artik her bayrak
# kumesi kendi dizinini kullanir; gecis aninda silme islemi yoktur.
FLAGS_HASH := $(shell printf '%s' '$(EXTRA_CFLAGS) $(VERSION_CFLAGS)' | md5sum | cut -c1-8)
BUILD = build/obj-$(FLAGS_HASH)
IMG_DIR = build

# -MMD -MP: header bagimlilik izleme — .h degisince ilgili .o otomatik yeniden
# derlenir (yoksa eski nesneler yeni struct tanimiyla ABI uyumsuz kalir).
COMMON = -std=gnu11 -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
         -Wall -Wextra -O2 -fno-pic -fno-asynchronous-unwind-tables \
         -mno-red-zone -fno-stack-protector -DNDEBUG -MMD -MP \
         $(VERSION_CFLAGS) $(EXTRA_CFLAGS)

INCLUDES = -Iinclude/arctian -Imain_kernel -Ikernel_plus/drivers -Ikernel_plus/net \
           -Ikernel_plus/tls \
           -Ikernel_plus/system -Idesktop -Idesktop/shell -Idesktop/ui-kit \
           -Idesktop/port -Idesktop/theme \
           -Iinstall -Ilib -Ithird_party -Ithird_party/lvgl \
           -Ithird_party/quickjs \
           -Ithird_party/lexbor/source \
           -Ithird_party/mbedtls/include -DLV_CONF_INCLUDE_SIMPLE


# Uretilen .d (bagimlilik) dosyalarini dahil et — hic yoksa bos birakir.
# ONEMLI: yalnizca bu bayrak kumesinin nesne dizini (BUILD) taranir.
# `find build` kullanildiginda SDK emulatorunun (build/emu) kendi dizinine
# GORE yazilmis .d dosyalari da iceri aliniyor ve make
# "No rule to make target ../../desktop/port/lv_port.c" hatasiyla duruyordu.
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

LIB_SRCS = lib/util.c lib/console.c lib/printf.c lib/alloc.c lib/storage.c \
           lib/serial.c lib/sbio.c lib/atapi.c lib/iso9660.c lib/media.c lib/fs.c lib/mmio.c \
           lib/blockdev.c lib/blk_ata.c lib/blk_ahci.c lib/blk_nvme.c lib/afs.c lib/fat32.c lib/klog.c

MAIN_C   = main_kernel/main.c main_kernel/loader.c main_kernel/idt.c \
           main_kernel/isr.c main_kernel/paging.c main_kernel/gdt.c \
           main_kernel/syscall.c main_kernel/ring3.c main_kernel/pic.c $(LIB_SRCS)
MAIN_ASM = main_kernel/entry.asm main_kernel/ring3_user.asm
ISR_STUB_OBJ = $(BUILD)/main_kernel/isr_stubs.o
RECOVER_OBJ  = $(BUILD)/lib/recover.o

INSTALL_C = install/installer.c $(LIB_SRCS)

KP_C = kernel_plus/init.c main_kernel/idt.c main_kernel/isr.c main_kernel/paging.c \
       main_kernel/syscall.c main_kernel/pic.c \
       main_kernel/loader.c $(LIB_SRCS) lib/mbedtls_port.c \
       $(wildcard kernel_plus/drivers/*.c) \
       $(wildcard kernel_plus/net/*.c) \
       $(wildcard kernel_plus/tls/*.c) \
       $(wildcard kernel_plus/system/*.c)

KP_ASM = kernel_plus/system/switch.S

# --- mbedTLS (TLS 1.3, Apache-2.0) ---
MBEDTLS_DIR  = third_party/mbedtls
MBEDTLS_SRCS = $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJS = $(patsubst $(MBEDTLS_DIR)/library/%.c,$(BUILD)/mbedtls/%.o,$(MBEDTLS_SRCS))
MBEDTLS_LIB  = $(BUILD)/mbedtls/libmbedtls.a

DESKTOP_C = desktop/main.c desktop/welcome.c desktop/dsk_state.c desktop/dsk_util.c desktop/windows.c desktop/launch.c desktop/startmenu.c desktop/network_panel.c desktop/power.c desktop/clock.c desktop/taskbar.c desktop/quickctl.c desktop/desktop_icons.c desktop/wallpaper.c desktop/dynapps.c desktop/settings.c desktop/shell_api.c desktop/store.c desktop/editor.c desktop/net_api.c desktop/selftest.c desktop/t06test.c desktop/t07test.c desktop/tour.c desktop/wlf.c desktop/wlf_host.c desktop/wlf_vm.c desktop/wlf_pack.c desktop/wolfc.c desktop/setup_wizard.c desktop/assistant.c desktop/perm.c desktop/consent.c desktop/sig.c desktop/slot.c desktop/port/lv_port.c desktop/theme/adl_wolf.c desktop/auth.c desktop/lock.c desktop/update.c lib/crt.c lib/png.c lib/svg.c
FONT_TTF  = design/font/Inter.ttf
FONT_ARIMO_TTF = design/font/ArimoBold.ttf
FONT_ARIMO_OBJ = $(BUILD)/desktop/assets/font_arimo.o
FONT_OBJ  = $(BUILD)/desktop/assets/font_data.o
FONT_ANTON_TTF = design/font/Anton-Regular.ttf
FONT_ANTON_OBJ = $(BUILD)/desktop/assets/font_anton.o
FONT_ABRIL_TTF = design/font/AbrilFatface-Regular.ttf
FONT_ABRIL_OBJ = $(BUILD)/desktop/assets/font_abril.o
FONT_SERIF_TTF = design/font/DejaVuSerif-Bold.ttf
FONT_SERIF_OBJ = $(BUILD)/desktop/assets/font_serif.o
# IBM Plex Sans (SIL OFL) - dock yazi tipi
FONT_PLEX_TTF = design/font/IBMPlexSans-Regular.ttf
FONT_PLEX_OBJ = $(BUILD)/desktop/assets/font_plex.o
FONT_PLEX_SB_TTF = design/font/IBMPlexSans-SemiBold.ttf
FONT_PLEX_SB_OBJ = $(BUILD)/desktop/assets/font_plex_sb.o
WALL_PNG  = design/wallpaper/back.png
WALL_OBJ  = $(BUILD)/desktop/assets/wallpaper_data.o
LOGO_PNG  = design/logo/logo.png
LOGO_OBJ  = $(BUILD)/desktop/assets/logo_data.o
SETUP_HTML = design/setup/Kurulum_Ozellestirme.html
SETUP_DATA_C = $(BUILD)/desktop/assets/setup_data.c
SETUP_DATA_OBJ = $(BUILD)/desktop/assets/setup_data.o
SVGRENDER = $(BUILD)/tools/svgrender

# Kullanici uygulamalari (desktop/apps/*)
APP_DIRS    = $(notdir $(wildcard desktop/apps/*))
APP_OBJS    = $(addprefix $(BUILD)/apps/,$(addsuffix .o,$(APP_DIRS)))
APPS_REG_C  = $(BUILD)/desktop/apps_registry.c
APPS_REG_OBJ = $(BUILD)/desktop/apps_registry.o
# Sadece ihtiyacimiz olan LVGL kaynaklari (yazilim render + cekirdek + temel widget'lar).
# Platform/OS/GPU/arka uc ve harici lib bagimliliklari haric tutulur.
LVGL_C = $(shell find third_party/lvgl/src -name '*.c' \
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
           ! -path '*/font/freetype/*' )

MAIN_OBJS    = $(patsubst %.c,$(BUILD)/%.o,$(MAIN_C)) $(patsubst %.asm,$(BUILD)/%.o,$(MAIN_ASM)) $(ISR_STUB_OBJ) $(RECOVER_OBJ)
INSTALL_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(INSTALL_C))
KP_OBJS      = $(patsubst %.c,$(BUILD)/%.o,$(KP_C)) $(patsubst %.S,$(BUILD)/%.o,$(KP_ASM)) $(ISR_STUB_OBJ) $(RECOVER_OBJ)
DESKTOP_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(DESKTOP_C)) $(patsubst %.c,$(BUILD)/%.o,$(LVGL_C)) $(FONT_OBJ) $(FONT_ANTON_OBJ) $(FONT_ABRIL_OBJ) $(FONT_SERIF_OBJ) $(FONT_ARIMO_OBJ) $(FONT_PLEX_OBJ) $(FONT_PLEX_SB_OBJ) $(WALL_OBJ) $(LOGO_OBJ) $(SETUP_DATA_OBJ) $(APP_OBJS) $(APPS_REG_OBJ) $(RECOVER_OBJ)

.PHONY: all boot main_kernel install kernel_plus desktop pack qemu qemu-iso clean store-packs FORCE

all: pack

FORCE:


boot:        $(BUILD)/boot/stage1.bin $(BUILD)/boot/stage1_iso.bin $(BUILD)/boot/stage2.bin
main_kernel: $(BUILD)/main_kernel/main_kernel.bin
install:     $(BUILD)/install/install.bin
kernel_plus: $(BUILD)/kernel_plus/kernel_plus.bin
desktop:     $(BUILD)/desktop/desktop.bin

pack: boot main_kernel install kernel_plus desktop
	ARCTIAN_OBJ=$(BUILD) python3 tools/arctianpack.py

# 0.7: uygulama bicimi yalnizca .wlf. Eski .dpk hatti kaldirildi: paketler
# desktop sembol ADRESLERINE mutlak linkleniyordu, bu yuzden desktop her
# yenilendiginde tum paketlerin yeniden uretilmesi gerekiyordu. .wlf native
# yalnizca wolf_api_t fonksiyon tablosunu gorur; adres kaymasi sorunu yoktur.
store-packs:
	@echo "  .dpk uretimi 0.7'de kaldirildi; uygulamalar .wlf olarak paketlenir"
	@echo "  bir ornek derle:  cd sdk/examples/uidemo && sh ../../build.sh"

debug:
	rm -f $(BUILD)/desktop/main.o $(BUILD)/desktop/lock.o $(BUILD)/install/installer.o $(BUILD)/kernel_plus/init.o
	$(MAKE) pack DEBUG=1
	cp -f build/arctian.iso build/arctian-debug.iso
	@echo "DEBUG ISO: build/arctian-debug.iso (kurulum + hesap olusturma atlandi)"

setuptest:
	rm -f $(BUILD)/desktop/main.o $(BUILD)/desktop/lock.o $(BUILD)/install/installer.o $(BUILD)/kernel_plus/init.o
	$(MAKE) pack DEBUG=1 SETUP_TEST=1
	@echo "SETUP TEST: kurulum ozellestirme ekrani dogrudan acilir"

$(BUILD)/boot/stage1.bin: boot/stage1.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin -o $@ $<

$(BUILD)/boot/stage1_iso.bin: boot/stage1_iso.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin -o $@ $<

$(BUILD)/boot/stage2.bin: boot/stage2.asm
	@mkdir -p $(dir $@)
	$(NASM) -f bin -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -c -o $@ $<

$(ISR_STUB_OBJ): main_kernel/isr.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -o $@ $<

LIBGCC = $(shell $(CC) -print-libgcc-file-name)

MBEDTLS_CFLAGS = $(filter-out -Wall -Wextra,$(COMMON)) -w -I$(MBEDTLS_DIR)/include

$(BUILD)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c
	@mkdir -p $(dir $@)
	$(CC) $(MBEDTLS_CFLAGS) -c -o $@ $<

$(MBEDTLS_LIB): $(MBEDTLS_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

# --- QuickJS (modern JS motoru, MIT) ---
QUICKJS_DIR  = third_party/quickjs
QUICKJS_CORE = quickjs cutils libregexp libunicode dtoa
QUICKJS_SRCS = $(addprefix $(QUICKJS_DIR)/,$(addsuffix .c,$(QUICKJS_CORE))) $(QUICKJS_DIR)/quickjs_port.c
QUICKJS_OBJS = $(patsubst $(QUICKJS_DIR)/%.c,$(BUILD)/quickjs/%.o,$(QUICKJS_SRCS))
# -Ilib: quickjs_port.c -> asi.h -> "klog.h" (lib/) cekirdek ortak basliklari icerir.
QUICKJS_CFLAGS = $(filter-out -Wall -Wextra,$(COMMON)) -w -I$(QUICKJS_DIR) -Iinclude/arctian -Ilib \
                 -DCONFIG_VERSION='"2025-09-13"'

$(BUILD)/quickjs/%.o: $(QUICKJS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(QUICKJS_CFLAGS) -c -o $@ $<

DESKTOP_OBJS += $(QUICKJS_OBJS)

LEXBOR_DIR    = third_party/lexbor
LEXBOR_OBJ    = $(BUILD)/lexbor/lexbor_single.o
LEXBOR_STUB   = $(BUILD)/lexbor/lex_posix_stub.o
LEXBOR_CFLAGS = $(filter-out -Wall -Wextra,$(COMMON)) -w -O1

$(LEXBOR_OBJ): $(LEXBOR_DIR)/lexbor_single.c
	@mkdir -p $(dir $@)
	$(CC) $(LEXBOR_CFLAGS) -c -o $@ $<

$(LEXBOR_STUB): $(LEXBOR_DIR)/lex_posix_stub.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) -w -c -o $@ $<

DESKTOP_OBJS += $(LEXBOR_OBJ) $(LEXBOR_STUB)

$(BUILD)/desktop/assets/font_data.c: $(FONT_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_TTF) $@ font_ttf

$(FONT_OBJ): $(BUILD)/desktop/assets/font_data.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_arimo.c: $(FONT_ARIMO_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_ARIMO_TTF) $@ font_arimo

$(FONT_ARIMO_OBJ): $(BUILD)/desktop/assets/font_arimo.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_anton.c: $(FONT_ANTON_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_ANTON_TTF) $@ font_anton

$(FONT_ANTON_OBJ): $(BUILD)/desktop/assets/font_anton.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_abril.c: $(FONT_ABRIL_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_ABRIL_TTF) $@ font_abril

$(FONT_ABRIL_OBJ): $(BUILD)/desktop/assets/font_abril.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_serif.c: $(FONT_SERIF_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_SERIF_TTF) $@ font_serif

$(FONT_SERIF_OBJ): $(BUILD)/desktop/assets/font_serif.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_plex.c: $(FONT_PLEX_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_PLEX_TTF) $@ font_plex

$(FONT_PLEX_OBJ): $(BUILD)/desktop/assets/font_plex.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/font_plex_sb.c: $(FONT_PLEX_SB_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_PLEX_SB_TTF) $@ font_plex_sb

$(FONT_PLEX_SB_OBJ): $(BUILD)/desktop/assets/font_plex_sb.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/wallpaper_data.c: $(WALL_PNG) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(WALL_PNG) $@ wallpaper_png

$(WALL_OBJ): $(BUILD)/desktop/assets/wallpaper_data.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/logo_data.c: $(LOGO_PNG) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(LOGO_PNG) $@ logo_png

$(LOGO_OBJ): $(BUILD)/desktop/assets/logo_data.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(SVGRENDER): tools/svgrender.c lib/svg.c lib/svg.h
	@mkdir -p $(dir $@)
	gcc -O2 -Ilib -o $@ tools/svgrender.c lib/svg.c -lm

$(SETUP_DATA_C): $(SETUP_HTML) tools/gen_setup_svg.py tools/gen_setup_png.py $(SVGRENDER)
	@mkdir -p $(dir $@)
	python3 tools/gen_setup_png.py $(SETUP_HTML) $(SVGRENDER) $@

$(SETUP_DATA_OBJ): $(SETUP_DATA_C)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(APPS_REG_C): FORCE tools/gen_apps.py
	@mkdir -p $(dir $@)
	ARCTIAN_OBJ=$(BUILD) python3 tools/gen_apps.py

$(APPS_REG_OBJ): $(APPS_REG_C)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

# C ile yazilmis uygulama
$(BUILD)/apps/%.o: desktop/apps/%/main.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -Dapp_entry=app_$*_entry -c -o $@ $<

# Zig ile yazilmis uygulama (freestanding x86_64). app_entry sembolu
# objcopy ile app_<isim>_entry olarak yeniden adlandirilir; boylece C
# uygulamalariyla tamamen ayni sekilde desktop'a linklenir.
$(BUILD)/apps/%.o: desktop/apps/%/main.zig
	@mkdir -p $(dir $@)
	$(ZIG) build-obj $< -target x86_64-freestanding -O ReleaseSmall -femit-bin=$@
	$(OBJCOPY) --redefine-sym app_entry=app_$*_entry $@

# Rust ile yazilmis uygulama (no_std, x86_64-unknown-none). Ayni sekilde
# app_entry sembolu app_<isim>_entry yapilir. Eksik core sembolleri icin
# tools/rust_compile_stub.sh kucuk stub'lar ekler.
$(BUILD)/apps/%.o: desktop/apps/%/main.rs
	@mkdir -p $(dir $@)
	bash tools/rust_compile_stub.sh $< $@
	$(OBJCOPY) --redefine-sym app_entry=app_$*_entry $@

$(BUILD)/main_kernel/main_kernel.elf: $(MAIN_OBJS)
	$(LD) -m elf_x86_64 -nostdlib -T main_kernel/linker.ld -o $@ $^
$(BUILD)/main_kernel/main_kernel.bin: $(BUILD)/main_kernel/main_kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/install/install.elf: $(INSTALL_OBJS)
	$(LD) -m elf_x86_64 -nostdlib -T install/linker.ld -o $@ $^
$(BUILD)/install/install.bin: $(BUILD)/install/install.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/kernel_plus/kernel_plus.elf: $(KP_OBJS) $(MBEDTLS_LIB)
	$(LD) -m elf_x86_64 -nostdlib -T kernel_plus/linker.ld -o $@ $^ $(LIBGCC)
$(BUILD)/kernel_plus/kernel_plus.bin: $(BUILD)/kernel_plus/kernel_plus.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/desktop/desktop.elf: $(DESKTOP_OBJS)
	$(LD) -m elf_x86_64 -nostdlib --wrap=lv_obj_send_event -T desktop/linker.ld -o $@ $^ $(LIBGCC)
$(BUILD)/desktop/desktop.bin: $(BUILD)/desktop/desktop.elf
	$(OBJCOPY) -O binary $< $@

qemu: pack
	$(QEMU) -drive file=build/arctian.img,format=raw -m 512M -vga std -serial stdio -no-reboot

qemu-iso: pack
	$(QEMU) -cdrom build/arctian.iso -boot d -m 512M -vga std -serial stdio -no-reboot

clean:
	rm -rf build


