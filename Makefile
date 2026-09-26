NASM    = nasm
CC      = gcc
LD      = ld
AR      = ar
OBJCOPY = objcopy
ZIG     ?= zig
RUSTC   ?= rustc
QEMU    = qemu-system-x86_64

EXTRA_CFLAGS ?=

COMMON = -std=gnu11 -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
         -Wall -Wextra -O2 -fno-pic -fno-asynchronous-unwind-tables \
         -mno-red-zone -fno-stack-protector -DNDEBUG $(EXTRA_CFLAGS)

INCLUDES = -Iinclude/arctian -Imain_kernel -Ikernel_plus/drivers -Ikernel_plus/net \
           -Ikernel_plus/tls \
           -Ikernel_plus/system -Idesktop -Idesktop/shell -Idesktop/ui-kit \
           -Idesktop/port -Idesktop/theme \
           -Iinstall -Ilib -Ithird_party -Ithird_party/lvgl \
           -Ithird_party/mbedtls/include -DLV_CONF_INCLUDE_SIMPLE

BUILD = build/obj

LIB_SRCS = lib/util.c lib/console.c lib/printf.c lib/alloc.c lib/storage.c \
           lib/serial.c lib/sbio.c lib/atapi.c lib/iso9660.c lib/media.c lib/fs.c lib/mmio.c \
           lib/blockdev.c lib/blk_ata.c lib/blk_ahci.c lib/blk_nvme.c lib/afs.c

MAIN_C   = main_kernel/main.c main_kernel/loader.c main_kernel/idt.c \
           main_kernel/isr.c main_kernel/pic.c $(LIB_SRCS)
MAIN_ASM = main_kernel/entry.asm
ISR_STUB_OBJ = $(BUILD)/main_kernel/isr_stubs.o

INSTALL_C = install/installer.c $(LIB_SRCS)

KP_C = kernel_plus/init.c main_kernel/idt.c main_kernel/isr.c main_kernel/pic.c \
       main_kernel/loader.c $(LIB_SRCS) lib/mbedtls_port.c \
       $(wildcard kernel_plus/drivers/*.c) \
       $(wildcard kernel_plus/net/*.c) \
       $(wildcard kernel_plus/tls/*.c) \
       kernel_plus/system/services.c

MBEDTLS_DIR  = third_party/mbedtls
MBEDTLS_SRCS = $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJS = $(patsubst $(MBEDTLS_DIR)/library/%.c,$(BUILD)/mbedtls/%.o,$(MBEDTLS_SRCS))
MBEDTLS_LIB  = $(BUILD)/mbedtls/libmbedtls.a

DESKTOP_C = desktop/main.c desktop/port/lv_port.c desktop/theme/adl_wolf.c lib/crt.c lib/png.c lib/svg.c
FONT_TTF  = design/font/Inter.ttf
FONT_OBJ  = $(BUILD)/desktop/assets/font_data.o
WALL_PNG  = design/wallpaper/back.png
WALL_OBJ  = $(BUILD)/desktop/assets/wallpaper_data.o

APP_DIRS    = $(notdir $(wildcard desktop/apps/*))
APP_OBJS    = $(addprefix $(BUILD)/apps/,$(addsuffix .o,$(APP_DIRS)))
APPS_REG_C  = $(BUILD)/desktop/apps_registry.c
APPS_REG_OBJ = $(BUILD)/desktop/apps_registry.o
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

MAIN_OBJS    = $(patsubst %.c,$(BUILD)/%.o,$(MAIN_C)) $(patsubst %.asm,$(BUILD)/%.o,$(MAIN_ASM)) $(ISR_STUB_OBJ)
INSTALL_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(INSTALL_C))
KP_OBJS      = $(patsubst %.c,$(BUILD)/%.o,$(KP_C)) $(ISR_STUB_OBJ)
DESKTOP_OBJS = $(patsubst %.c,$(BUILD)/%.o,$(DESKTOP_C)) $(patsubst %.c,$(BUILD)/%.o,$(LVGL_C)) $(FONT_OBJ) $(WALL_OBJ) $(APP_OBJS) $(APPS_REG_OBJ)

.PHONY: all boot main_kernel install kernel_plus desktop pack qemu clean FORCE

all: pack

FORCE:

boot:        $(BUILD)/boot/stage1.bin $(BUILD)/boot/stage1_iso.bin $(BUILD)/boot/stage2.bin
main_kernel: $(BUILD)/main_kernel/main_kernel.bin
install:     $(BUILD)/install/install.bin
kernel_plus: $(BUILD)/kernel_plus/kernel_plus.bin
desktop:     $(BUILD)/desktop/desktop.bin

pack: boot main_kernel install kernel_plus desktop
	python3 tools/arctianpack.py

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

$(BUILD)/desktop/assets/font_data.c: $(FONT_TTF) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(FONT_TTF) $@ font_ttf

$(FONT_OBJ): $(BUILD)/desktop/assets/font_data.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/desktop/assets/wallpaper_data.c: $(WALL_PNG) tools/bin2c.py
	@mkdir -p $(dir $@)
	python3 tools/bin2c.py $(WALL_PNG) $@ wallpaper_png

$(WALL_OBJ): $(BUILD)/desktop/assets/wallpaper_data.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(APPS_REG_C): FORCE tools/gen_apps.py
	@mkdir -p $(dir $@)
	python3 tools/gen_apps.py

$(APPS_REG_OBJ): $(APPS_REG_C)
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -c -o $@ $<

$(BUILD)/apps/%.o: desktop/apps/%/main.c
	@mkdir -p $(dir $@)
	$(CC) $(COMMON) $(INCLUDES) -Dapp_entry=app_$*_entry -c -o $@ $<

$(BUILD)/apps/%.o: desktop/apps/%/main.zig
	@mkdir -p $(dir $@)
	$(ZIG) build-obj $< -target x86_64-freestanding -O ReleaseSmall -femit-bin=$@
	$(OBJCOPY) --redefine-sym app_entry=app_$*_entry $@

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
	$(LD) -m elf_x86_64 -nostdlib -T desktop/linker.ld -o $@ $^
$(BUILD)/desktop/desktop.bin: $(BUILD)/desktop/desktop.elf
	$(OBJCOPY) -O binary $< $@

qemu: pack
	$(QEMU) -drive file=build/arctian.img,format=raw -m 512M -vga std -serial stdio -no-reboot

clean:
	rm -rf build
